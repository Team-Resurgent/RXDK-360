#!/usr/bin/env python3
"""Compile a subset of picolibc for the Xbox 360 target into an ELF archive.

The start of the modern C23/C++23 runtime that replaces the translated 2013 MS
CRT (libcMT). It builds picolibc sources from vendor/picolibc with the patched
clang (powerpc-unknown-xbox360, the real MS-PPC ABI) straight to PPC ELF objects
-- no COFF translation, since we have the source -- and archives them into
build/libc/libc.a.

Brought up piece by piece, keeping the corpus (tools/run_corpus.py) green:
  * string / mem / ctype / errno  -- done
  * stdio (self-contained sprintf, incl. the MSVC %S rewrite) -- next
  * malloc (console pool ExAllocatePool)  -- done
  * C++ runtime                    -- later

Usage:
    python tools/build_libc.py [-o build/libc/libc.a]
"""
import argparse
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PICO = os.path.join(ROOT, "vendor", "picolibc")
CONFIG = os.path.join(ROOT, "runtime", "config")

CLANG = os.environ.get("RXDK_CLANG", os.path.join(ROOT, "build", "llvm", "bin", "clang.exe"))
AR = os.environ.get("RXDK_AR", r"C:\Program Files\LLVM\bin\llvm-ar.exe")
TRIPLE = "powerpc-unknown-xbox360"

# picolibc's own flags (mirrors RXDK-Libs build/xbox_target.zig picolibcFlags):
# -fno-builtin is essential -- picolibc's memcpy/strlen ARE the builtins, so
# letting clang recognise their loops as a memcpy/strlen idiom is infinite
# self-recursion; -include picolibc.h force-includes the config.
FLAGS = [
    "--target=" + TRIPLE, "-std=c17", "-O2", "-ffreestanding",
    "-fno-stack-protector", "-fno-zero-initialized-in-bss",
    "-fno-sanitize=all", "-fno-builtin", "-Wno-everything",
    "-D__Picolibc__", "-D__TINY_STDIO",
    "-include", "picolibc.h",
]
INCLUDES = [
    "-I" + CONFIG,
    "-I" + os.path.join(PICO, "libc", "include"),
    "-I" + os.path.join(PICO, "libc", "stdio"),
    "-I" + os.path.join(PICO, "libc", "locale"),   # locale_private.h, internal
    "-I" + os.path.join(PICO, "libc", "ctype"),    # ctype local.h, internal
    "-I" + os.path.join(PICO, "libm", "common"),   # math_config.h / fdlibm internals
]

# Source subdirectories globbed wholesale (with per-file excludes below). Grows
# as more of the runtime is brought up.
SUBDIRS = [
    "libc/string",
    "libc/ctype",
    "libc/errno",
    "libc/stdio",
]

# libm: full double + float math (libm/common + libm/math), the same wholesale
# glob RXDK-Libs uses on the original Xbox. The C++ STL needs it (<cmath>, and
# libc++'s hash table sizing calls ceilf). Only the soft long-double helpers
# (sl_* prefix) are skipped; the l-suffixed long-double entry points are kept, as
# on the original Xbox. Object files are parent-qualified (in main) so the two
# dirs' same-named members (and any libc clashes) don't collide in the flat objdir.
LIBM_SUBDIRS = [
    "libm/common",
    "libm/math",
]

# Mirrors RXDK-Libs' libm excludes. In libm/common the transcendental *f and
# double cores duplicate the authoritative implementations in libm/math, so drop
# the common copies; gamma has no standalone TU.
LIBM_EXCLUDE = set([
    "cosf.c", "sinf.c", "sincosf.c",
    "exp.c", "exp2.c", "log.c", "log2.c", "pow.c", "s_log2.c",
    "sf_exp.c", "sf_exp2.c", "sf_log.c", "sf_log2.c", "sf_pow.c",
    "s_gamma.c", "sf_gamma.c",
])

# Extra (non-picolibc) glue. The .c files compile with the picolibc flags; the
# .cpp C++ runtime compiles with the C++ flag set below.
XBOX_GLUE = [
    os.path.join(ROOT, "runtime", "xbox", "ms_printf.c"),           # MSVC %S/%C/%I64 rewrite
    os.path.join(ROOT, "runtime", "xbox", "rt_support.c"),          # 64-bit div + console-pool malloc
    os.path.join(ROOT, "runtime", "xbox", "cxxrt.cpp"),             # operator new/delete + __cxa_*
    os.path.join(ROOT, "runtime", "xbox", "libc_hooks.c"),          # stdin/output/exec hooks + POSIX backend
    os.path.join(ROOT, "runtime", "xbox", "posix_stdio_streams.c"), # stdin/stdout/stderr FILE globals
    os.path.join(ROOT, "runtime", "xbox", "locks.c"),              # kernel critical-section retargetable locks
    os.path.join(ROOT, "runtime", "xbox", "threads.c"),            # C11 <threads.h> over the kernel (thrd/mtx/tss/once)
    os.path.join(ROOT, "runtime", "xbox", "crt_start.c"),          # standard main() CRT startup (_start)
    os.path.join(ROOT, "runtime", "xbox", "clock.c"),              # clock_gettime over KeQuerySystemTime (for <chrono>)
]

# C++ runtime glue: no picolibc config force-include; freestanding, no EH/RTTI yet.
CPP_FLAGS = [
    "--target=" + TRIPLE, "-std=c++23", "-O2", "-ffreestanding",
    "-fno-exceptions", "-fno-rtti", "-fno-stack-protector",
    "-fno-sanitize=all", "-fno-builtin", "-Wno-everything",
]

# Files in the globbed subdirs we do not want.
EXCLUDE = set([
    # tinystdio's vfprintf.c #includes these split parts, so they are not
    # standalone translation units.
    "conv_flt.c", "ultoa_invert.c",
    "vfprintf_char.c", "vfprintf_float.c", "vfprintf_int.c",
    "vfprintf_n.c", "vfprintf_str.c",
    # the Ryu float<->string engines duplicate the classic dtoa/ftoa engines
    # (which back the double printf variant), so keep only the classic ones.
    "ftoa_ryu.c", "dtoa_ryu.c", "atod_ryu.c", "atof_ryu.c",
    "ryu_divpow2.c", "ryu_log10.c", "ryu_log2pow5.c", "ryu_pow5bits.c",
    "ryu_table.c", "ryu_umul128.c",
    # replaced by runtime/xbox/ms_printf.c, which translates the MSVC format
    # (%S/%C/%I64 ...) before formatting over the same vfprintf engine.
    "sprintf.c", "snprintf.c", "swprintf.c", "vsnprintf.c",
    # picolibc's stdin/stdout/stderr use __weak_reference aliases lld does not
    # apply; runtime/xbox/posix_stdio_streams.c provides strong FILE* globals.
    "posixiob_stdin.c", "posixiob_stdout.c", "posixiob_stderr.c",
])


# Files our patched clang miscompiles at -O2 with the MachineFunction
# PeepholeOptimizer enabled -- compiled with -mllvm -disable-peephole (still -O2
# otherwise). Root-caused by bisecting the backend passes: the float-conversion
# branch of vfprintf.c came out writing nothing past the conversion (%f/%g/%e/%Lf
# -> snprintf("A%fB",3.5) == "A", with the length still counted, so the output
# FILE's put() had stopped landing characters). peephole's optimizeCompareInstr
# rewrites are each individually correct (RLWINM+CMPLWI -> ANDI_rec; "x<1" ->
# "x<=0"), but they turn plain compares into record-form (dot) instructions with
# physical $cr0 def + COPY $cr0 chains, and a later -O2 pass mishandles that
# $cr0 density here. -disable-peephole removes the trigger; the double float
# engine, FP varargs and %d/%s are all fine without it. TODO: chase the downstream
# physical-$cr0 codegen bug and drop this.
NO_PEEPHOLE = set([
    "vfprintf.c",
])


def sources():
    out = []
    for sub in SUBDIRS:
        for f in sorted(glob.glob(os.path.join(PICO, sub, "*.c"))):
            if os.path.basename(f) in EXCLUDE:
                continue
            out.append(f)
    for sub in LIBM_SUBDIRS:
        for f in sorted(glob.glob(os.path.join(PICO, sub, "*.c"))):
            b = os.path.basename(f)
            if b in LIBM_EXCLUDE or b.startswith("sl_"):
                continue
            out.append(f)
    out += XBOX_GLUE
    return out


# The MS out-of-line register save/restore helpers (__savegprlr_N/__restgprlr_N,
# __savefpr_N/__restfpr_N) are pure ABI glue that every MS-compiled prebuilt lib
# calls and that picolibc does not provide (our own clang inlines its saves).
# Pull the verified objects from the translated libcMT rather than hand-writing
# the sequences -- reusing the shipped glue, like the rest of the toolchain.
LIBCMT = os.path.join(ROOT, "build", "coff", "libcMT.a")
MS_GLUE_MEMBERS = ("crtgpr.o", "crtfpr.o")


def extract_ms_glue(objdir):
    if not os.path.exists(LIBCMT):
        print("  note: %s not built, MS register helpers not bundled" % LIBCMT)
        return []
    sys.path.insert(0, HERE)
    from coff2elf import read_archive
    out = []
    for member, _ln in read_archive(open(LIBCMT, "rb").read()):
        base = member.name.rstrip("/")
        if base in MS_GLUE_MEMBERS:
            p = os.path.join(objdir, "msglue_" + base)
            with open(p, "wb") as f:
                f.write(member.data)
            out.append(p)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "build", "libc", "libc.a"))
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(CLANG):
        sys.exit("patched clang not found: %s (build tools/build-llvm.bat)" % CLANG)

    objdir = os.path.join(os.path.dirname(args.out), "obj")
    os.makedirs(objdir, exist_ok=True)

    objs, failed = [], []
    for src in sources():
        # Qualify the object with its parent directory: several picolibc trees
        # (libm/common vs libm/math especially) carry same-named members that
        # would otherwise overwrite each other in the flat objdir.
        parent = os.path.basename(os.path.dirname(os.path.abspath(src)))
        obj = os.path.join(objdir, parent + "_" + os.path.splitext(os.path.basename(src))[0] + ".o")
        if os.path.splitext(src)[1].lower() in (".cpp", ".cc", ".cxx"):
            cmd = [CLANG] + CPP_FLAGS + ["-c", src, "-o", obj]
        else:
            flags = FLAGS
            if os.path.basename(src) in NO_PEEPHOLE:
                flags = FLAGS + ["-mllvm", "-disable-peephole"]
            cmd = [CLANG] + flags + INCLUDES + ["-c", src, "-o", obj]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            failed.append((src, r.stderr.strip().splitlines()[-1:] or [""]))
            if args.verbose:
                print("FAIL %s\n%s" % (src, r.stderr))
        else:
            objs.append(obj)

    if failed:
        print("%d source(s) failed to compile:" % len(failed))
        for src, why in failed:
            print("  %s: %s" % (os.path.relpath(src, ROOT), why[0] if why else ""))
        sys.exit(1)

    objs += extract_ms_glue(objdir)

    if os.path.exists(args.out):
        os.remove(args.out)
    r = subprocess.run([AR, "rcs", args.out] + objs, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("archive failed:\n" + r.stderr)
    print("wrote %s: %d objects" % (os.path.relpath(args.out, ROOT), len(objs)))


if __name__ == "__main__":
    main()
