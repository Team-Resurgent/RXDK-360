#!/usr/bin/env python3
"""Build an Xbox 360 title end to end: compile -> resolve kernel imports ->
link -> pack, in one command.

This folds the manual dance (zig cc -c / gen_import_stubs / zig cc link with a
hand-written layout script / elf2xex) into a single step. It:

  1. compiles each C/C++/asm source (prebuilt .o files pass through),
  2. writes a linker script that lays the image out the way the packer needs --
     room below the first section for the synthesised PE headers, the import
     thunks in their own section past a small gap (so no thunk abuts the entry's
     basic block), and the writable region (.data/.bss) aligned to its own page
     so per-section descriptors can map it read-write,
  3. trial-links to discover the still-undefined symbols (the kernel imports),
     resolves their ordinals from the XDK import libraries and emits the linkable
     thunks plus an import manifest,
  4. links the final ELF against the objects, the stubs and any libraries, and
  5. packs it into a XEX.

Usage:
    python tools/mktitle.py build/ctitle/apidata.c -o build/ctitle/apidata.xex \\
        --lib xapilib,libcMT

Libraries named without a path or extension are resolved to <coff-dir>/<name>.a
(the archives coff2elf.py produces from the XDK .libs).
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

TARGET = "powerpc-freestanding-none"
MS_TRIPLE = "powerpc-unknown-xbox360"                  # the patched-clang MS-ABI target
DEFAULT_XDK = r"C:\Program Files (x86)\Microsoft Xbox 360 SDK\lib\xbox"
DEFAULT_CLANG = os.environ.get("RXDK_CLANG",
                               os.path.join(ROOT, "build", "llvm", "bin", "clang.exe"))
DEFAULT_BASE = 0x82000000
CFLAGS = ["-target", TARGET, "-O2", "-fno-sanitize=all"]


def zig():
    return os.environ.get("ZIG", "zig")


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def page_size_for(base):
    """Match the packer: 0x80000000-0x8FFFFFFF pages in 64KB, 0x90000000+ in 4KB."""
    return 0x10000 if base < 0x90000000 else 0x1000


def write_layout(path, base, page):
    """The layout the packer expects, parameterised by load base and page size."""
    with open(path, "w", newline="\n") as f:
        f.write(f"""ENTRY(_start)
SECTIONS {{
  . = 0x{base:08X};
  . += 0x1000;                       /* room for the synthesised PE headers */
  .text   : {{ *(.text*) }}
  . = ALIGN(0x{page:X});             /* read-only data on its own page: keeps it out
                                        of the CODE page so xenia's code analyser
                                        does not disassemble format strings as code */
  .rodata : {{
    *(.rodata*)
    /* Merge the per-function LSDA sections (-fexceptions emits one
       .gcc_except_table.<mangled> per function). Without an explicit rule lld
       leaves each as its own orphan section -- a C++/EH-heavy title (e.g. one
       using <iostream>) then has hundreds of sections, and the synthesised PE
       header outgrows the headroom below the first section. Gathering them here
       (read-only, addressed by the FDE, so any loaded section works) keeps the
       section count -- and the header -- small for every title, automatically. */
    *(.gcc_except_table .gcc_except_table.*)
  }}
  . = ALIGN(0x{page:X});             /* DWARF EH tables on their own read-only page:
                                        libunwind recovers the section's true length
                                        from the PE section table at runtime, so it
                                        must be a distinct section (not merged with
                                        .rodata) and its markers give the start. */
  .eh_frame : {{
    PROVIDE_HIDDEN(__eh_frame_start = .);
    KEEP(*(.eh_frame))
    KEEP(*(.eh_frame.*))
    PROVIDE_HIDDEN(__eh_frame_end = .);
    /* No .eh_frame_hdr (lld does not synthesise one for this PE target); give
       libunwind's DWARF-index path empty start==end markers so it falls back to
       a linear .eh_frame scan. */
    PROVIDE_HIDDEN(__eh_frame_hdr_start = .);
    PROVIDE_HIDDEN(__eh_frame_hdr_end = .);
  }}
  .init_array : {{                   /* C++ static constructors, run pre-main by start.c */
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN(__init_array_end = .);
  }}
  .fini_array : {{                   /* destructors -- gather here (writable) so they
                                        do not land in the .kthunks CODE page and flip
                                        it to a data page (which breaks bl <thunk>) */
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    PROVIDE_HIDDEN(__fini_array_end = .);
  }}
  .CRT : {{                          /* MS CRT initializer table (.CRT$XCA..XCZ),
                                        how prebuilt MS libs register pre-main
                                        constructors; run by start.c via __xc_a/z */
    PROVIDE_HIDDEN(__xc_a = .);
    KEEP(*(SORT_BY_NAME(.CRT$XC*)))
    PROVIDE_HIDDEN(__xc_z = .);
  }}
  .kvars  : {{ KEEP(*(.kvars)) }}    /* import var records: keep past --gc-sections */
  . = ALIGN(0x{page:X});             /* import thunks on their own CODE page, clear
                                        of both the entry code and the rodata */
  .kthunks : ALIGN(16) {{ KEEP(*(.kthunks)) }}
  . = ALIGN(0x{page:X});             /* writable region on its own page(s) */
  .data : {{ *(.data*) }}
  .bss  : {{ *(.bss*) *(COMMON) }}
  /DISCARD/ : {{ *(.comment) *(.note*) }}
}}
""")


def compile_sources(sources, workdir, cc, clang, cflags):
    """Compile each source to an object; pass prebuilt .o through.

    cc == "zig"   -> zig cc, the PPC EABI (works for simple titles);
    cc == "clang" -> the patched clang targeting powerpc-unknown-xbox360, which
                     emits the real MS-PPC ABI the shipped libraries expect.

    cflags are extra compile flags (include dirs, -fno-exceptions, ...) appended
    to the C/C++ compile of every non-assembly source.
    """
    objects = []
    for src in sources:
        ext = os.path.splitext(src)[1].lower()
        if ext == ".o":
            objects.append(src)
            continue
        # unique object name: many titles have several sources all called main.c
        # (one per test dir), so qualify the object with its parent directory.
        stem = os.path.splitext(os.path.basename(src))[0]
        parent = os.path.basename(os.path.dirname(os.path.abspath(src)))
        obj = os.path.join(workdir, (parent + "_" + stem if parent else stem) + ".o")
        is_asm = ext in (".s", ".asm")
        is_cpp = ext in (".cpp", ".cc", ".cxx", ".c++")
        if cc == "clang":
            cmd = [clang, "--target=" + MS_TRIPLE, "-c", src, "-o", obj]
            if not is_asm:
                # titles get the modern standard the runtime targets (C23/C++23);
                # picolibc itself is built separately at c17 (build_libc.py).
                std = "-std=c++23" if is_cpp else "-std=c23"
                cmd[2:2] = ["-O2", std] + cflags
        elif is_asm:                                   # zig: assembly, no C-only flags
            cmd = [zig(), "cc", "-target", TARGET, "-c", src, "-o", obj]
        else:
            cmd = [zig(), "cc"] + CFLAGS + cflags + ["-c", src, "-o", obj]
        r = run(cmd)
        if r.returncode != 0:
            sys.exit(f"compile failed for {src}:\n{r.stderr}")
        objects.append(obj)
    return objects


def link(objects, libs, stubs, layout, out_elf, gc=True, ldflags=()):
    """Link objects (+ optional stubs .s + libs) into the ELF, return the result."""
    cmd = [zig(), "cc", "-target", TARGET, "-nostdlib",
           "-Wl,-T," + layout]
    if gc:
        cmd.append("-Wl,--gc-sections")
    cmd += list(ldflags)
    cmd += objects
    if stubs:
        cmd.append(stubs)
    cmd += libs
    cmd += ["-o", out_elf]
    return run(cmd)


UNDEFINED_RE = re.compile(r"undefined symbol: (\S+)")


def undefined_from(link_result):
    """Parse the undefined-symbol names from a failed link."""
    text = link_result.stdout + link_result.stderr
    seen = []
    for name in UNDEFINED_RE.findall(text):
        if name not in seen:
            seen.append(name)
    return seen


def resolve_libs(names, coff_dir):
    out = []
    for n in names:
        if os.sep in n or n.endswith(".a") or os.path.isabs(n):
            out.append(n)
        else:
            out.append(os.path.join(coff_dir, n + ".a"))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sources", nargs="+", help="C/C++/asm sources or prebuilt .o")
    ap.add_argument("-o", "--out", required=True, help="output .xex")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=DEFAULT_BASE,
                    help="load base address (default 0x82000000)")
    ap.add_argument("--lib", action="append", default=[],
                    help="library archive(s), comma-separated; a bare name -> "
                         "<coff-dir>/<name>.a")
    ap.add_argument("--coff-dir", default=os.path.join(ROOT, "build", "coff"),
                    help="where bare --lib names resolve (coff2elf archives)")
    ap.add_argument("--xdk", default=DEFAULT_XDK, help="XDK lib\\xbox directory")
    ap.add_argument("--cc", choices=("zig", "clang"), default="zig",
                    help="compiler: zig (PPC EABI) or clang (patched MS-PPC ABI)")
    ap.add_argument("--clang", default=DEFAULT_CLANG,
                    help="patched clang path (for --cc clang)")
    ap.add_argument("--cflag", action="append", default=[],
                    help="extra compile flag (repeatable), e.g. --cflag -Iinc "
                         "--cflag -fno-exceptions")
    ap.add_argument("--ldflag", action="append", default=[],
                    help="extra linker flag (repeatable), passed to the driver, "
                         "e.g. --ldflag=-Wl,--allow-multiple-definition")
    ap.add_argument("--keep-elf", action="store_true",
                    help="keep the intermediate .elf next to the output")
    args = ap.parse_args()

    if args.cc == "clang" and not os.path.exists(args.clang):
        sys.exit(f"patched clang not found: {args.clang} (build tools/build-llvm.bat)")

    out_base = os.path.splitext(args.out)[0]
    workdir = os.path.dirname(os.path.abspath(args.out)) or "."
    os.makedirs(workdir, exist_ok=True)
    page = page_size_for(args.base)

    libnames = [n for spec in args.lib for n in spec.split(",") if n]
    libs = resolve_libs(libnames, args.coff_dir)
    for lib in libs:
        if not os.path.exists(lib):
            sys.exit(f"library not found: {lib}")

    objects = compile_sources(args.sources, workdir, args.cc, args.clang, args.cflag)

    layout = out_base + ".ld"
    write_layout(layout, args.base, page)

    elf = out_base + ".elf"

    # trial link (no stubs) to discover the undefined kernel imports
    trial = link(objects, libs, None, layout, elf, gc=True, ldflags=args.ldflag)
    undefined = undefined_from(trial) if trial.returncode != 0 else []

    manifest = None
    stubs = None
    if undefined:
        stubs = out_base + "_stubs.s"
        manifest = out_base + "_stubs.json"
        r = run([sys.executable, os.path.join(HERE, "gen_import_stubs.py"),
                 "--xdk", args.xdk, "--names", ",".join(undefined),
                 "-o", stubs, "--manifest", manifest])
        sys.stdout.write(r.stdout)
        if r.returncode != 0:
            sys.exit(r.stderr or "gen_import_stubs failed")
        # gen_import_stubs prints "unresolved (not kernel imports): a, b" for
        # any undefined that is not a kernel export -- a real link error.
        m = re.search(r"unresolved \(not kernel imports\): (.+)", r.stdout)
        if m:
            sys.exit(f"unresolved symbols (not kernel imports): {m.group(1).strip()}")

    # final link
    final = link(objects, libs, stubs, layout, elf, gc=True, ldflags=args.ldflag)
    if final.returncode != 0:
        sys.exit(f"link failed:\n{final.stdout}{final.stderr}")

    # pack
    cmd = [sys.executable, os.path.join(HERE, "elf2xex.py"), elf, "-o", args.out]
    if manifest:
        cmd += ["--import-manifest", manifest]
    r = run(cmd)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.exit(r.stderr or "elf2xex failed")

    if not args.keep_elf and os.path.exists(elf):
        os.remove(elf)


if __name__ == "__main__":
    main()
