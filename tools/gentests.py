#!/usr/bin/env python3
"""Build xenia PPC test artefacts with the RXDK-360 toolchain.

Xenia's PPC test runner (xenia-cpu-ppc-tests) executes real PowerPC code and
checks register results, which makes it a far better correctness harness for a
code generator than launching the full emulator. Each test needs three files:

    <name>.s     annotated source, read by the runner for REGISTER_IN/OUT
    <name>.bin   raw image, loaded at START_ADDRESS (0x80000000)
    <name>.map   nm-style symbol map: "<hex offset> t test_<name>"

Upstream generates the .bin and .map with a custom binutils built by
`xb gentests`. This produces them with our own clang and lld instead, so no
extra toolchain is needed, and so tests can be written in C and compiled by the
compiler actually under test.

A C input may carry annotations in comments, which are injected into the
generated assembly next to the matching label:

    //#_ REGISTER_IN r3 5
    //#_ REGISTER_IN r4 7
    int test_add(int a, int b) { return a + b; }
    //#_ REGISTER_OUT r3 12

Usage:
    python tools/gentests.py <test.c|test.s> [-o OUTDIR] [--clang PATH]
"""
import argparse
import os
import re
import struct
import subprocess
import sys

START_ADDRESS = 0x80000000
TRIPLE = "powerpc-unknown-xbox360"


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"failed: {' '.join(cmd)}\n{r.stdout}{r.stderr}")
    return r


# ---- ELF32 big-endian reading ----------------------------------------------

def _u(fmt, data, off):
    return struct.unpack_from(">" + fmt, data, off)


def elf_image(data):
    """Flatten the PT_LOAD segments into one image based at the lowest vaddr."""
    e_phoff, = _u("I", data, 0x1C)
    e_phentsize, e_phnum = _u("HH", data, 0x2A)
    segs = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz = _u("IIIIII", data, o)
        if p_type == 1 and p_memsz:            # PT_LOAD
            segs.append((p_vaddr, p_offset, p_filesz, p_memsz))
    if not segs:
        sys.exit("no PT_LOAD segments in the linked image")
    base = min(s[0] for s in segs)
    end = max(s[0] + s[3] for s in segs)
    img = bytearray(end - base)
    for vaddr, off, filesz, _memsz in segs:
        img[vaddr - base:vaddr - base + filesz] = data[off:off + filesz]
    return base, bytes(img)


def elf_test_symbols(data):
    """Return [(offset, name)] for local symbols named test_*, in address order."""
    e_shoff, = _u("I", data, 0x20)
    e_shentsize, e_shnum, e_shstrndx = _u("HHH", data, 0x2E)

    def sh(i):
        o = e_shoff + i * e_shentsize
        name, typ, flags, addr, off, size, link, info, align, entsize = _u(
            "IIIIIIIIII", data, o)
        return dict(type=typ, addr=addr, off=off, size=size, link=link,
                    entsize=entsize)

    out = []
    for i in range(e_shnum):
        s = sh(i)
        if s["type"] != 2:                     # SHT_SYMTAB
            continue
        strtab = sh(s["link"])
        n = s["size"] // s["entsize"]
        for k in range(n):
            o = s["off"] + k * s["entsize"]
            st_name, st_value, _st_size, st_info, _st_other, _st_shndx = _u(
                "IIIBBH", data, o)
            end = data.index(b"\0", strtab["off"] + st_name)
            name = data[strtab["off"] + st_name:end].decode("utf-8", "replace")
            if not name.startswith("test_"):
                continue
            if (st_info >> 4) != 0:            # STB_LOCAL only: the runner
                continue                       # matches " t test_"
            out.append((st_value, name))
    return sorted(set(out))


# ---- annotation injection ---------------------------------------------------

ANNOT = re.compile(r"^\s*//#_\s*(.*\S)\s*$")
CFUNC = re.compile(r"^\s*(?:static\s+)?[A-Za-z_][\w \t\*]*\b(test_\w+)\s*\(")


def collect_annotations(c_src):
    """Map each test function to its #_ lines.

    Annotations before a definition belong to it, as do those following it.
    A blank line closes a function's scope, so the next block of annotations
    belongs to whatever comes after.
    """
    pending, per_fn, current = [], {}, None
    for line in c_src.splitlines():
        m = ANNOT.match(line)
        if m:
            if current:
                per_fn[current].append(m.group(1))
            else:
                pending.append(m.group(1))
            continue
        if not line.strip():          # blank line closes the current scope
            current = None
            continue
        f = CFUNC.match(line)
        if f:
            current = f.group(1)
            per_fn[current] = pending + per_fn.get(current, [])
            pending = []
        elif current is None:
            pending = []              # ordinary code discards stale annotations
    return per_fn


def inject(asm, per_fn):
    """Place each function's annotations directly after its label."""
    out = []
    for line in asm.splitlines():
        out.append(line)
        m = re.match(r"^(test_\w+):", line)
        if m and m.group(1) in per_fn:
            for a in per_fn[m.group(1)]:
                out.append(f"  #_ {a}")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="test source (.c or .s)")
    ap.add_argument("-o", "--outdir", default=".",
                    help="where to write .s/.bin/.map (default: current directory)")
    ap.add_argument("--clang", default=os.environ.get(
        "RXDK_CLANG", r"D:\Git\RXDK-360\build\llvm\bin\clang.exe"))
    ap.add_argument("--lld", default=os.environ.get(
        "RXDK_LLD", r"D:\Git\RXDK-360\build\llvm\bin\ld.lld.exe"))
    ap.add_argument("--cflags", default="-O2 -ffreestanding -fomit-frame-pointer",
                    help="extra compiler flags")
    ap.add_argument("--run", nargs="?", const=os.environ.get(
        "XENIA_PPC_TESTS",
        r"D:\Git\xenia-canary\build\bin\Windows\Debug\xenia-cpu-ppc-tests.exe"),
        help="after building, execute the tests with xenia's PowerPC runner")
    args = ap.parse_args()

    src = os.path.abspath(args.source)
    name = os.path.splitext(os.path.basename(src))[0]
    if not name.startswith("instr_"):
        sys.exit("xenia discovers only tests named instr_*.s; rename "
                 + os.path.basename(src) + " accordingly")
    outdir = os.path.abspath(args.outdir)
    os.makedirs(outdir, exist_ok=True)
    s_path = os.path.join(outdir, name + ".s")
    obj = os.path.join(outdir, name + ".o")
    elf = os.path.join(outdir, name + ".elf")
    bin_path = os.path.join(outdir, name + ".bin")
    map_path = os.path.join(outdir, name + ".map")

    base = [args.clang, "--target=" + TRIPLE] + args.cflags.split()

    if src.endswith(".c"):
        # Compile to assembly, then carry the annotations across so the runner
        # can read them from the .s it is given.
        run(base + ["-S", src, "-o", s_path])
        asm = open(s_path, encoding="utf-8", errors="replace").read()
        annotations = collect_annotations(open(src, encoding="utf-8").read())
        open(s_path, "w", encoding="utf-8").write(inject(asm, annotations))
    else:
        if os.path.abspath(src) != s_path:
            open(s_path, "w", encoding="utf-8").write(
                open(src, encoding="utf-8", errors="replace").read())

    run(base + ["-c", s_path, "-o", obj])
    run([args.lld, "-e", "0", "--image-base=%d" % START_ADDRESS,
         "--no-rosegment", "-o", elf, obj])

    data = open(elf, "rb").read()
    load_base, img = elf_image(data)
    open(bin_path, "wb").write(img)

    syms = elf_test_symbols(data)
    if not syms:
        sys.exit("no local test_* symbols found; the runner matches ' t test_', "
                 "so test labels must not be .globl")
    with open(map_path, "w", encoding="utf-8", newline="\n") as f:
        for addr, sym in syms:
            f.write(f"{addr - load_base:016x} t {sym}\n")

    os.remove(obj)
    print(f"{name}: {len(img)} bytes at 0x{START_ADDRESS:08X}, {len(syms)} tests")
    for addr, sym in syms:
        print(f"  +0x{addr - load_base:04X}  {sym}")
    print(f"wrote {s_path}\n      {bin_path}\n      {map_path}")

    if args.run:
        if not os.path.exists(args.run):
            sys.exit(f"test runner not found: {args.run}")
        print()
        return subprocess.run([args.run, "--test_path=" + outdir + os.sep,
                               "--test_bin_path=" + outdir + os.sep]).returncode


if __name__ == "__main__":
    sys.exit(main() or 0)
