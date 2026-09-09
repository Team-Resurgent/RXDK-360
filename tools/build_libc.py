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
  * malloc, C++ runtime           -- later

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
]

# Source subdirectories globbed wholesale (with per-file excludes below). Grows
# as more of the runtime is brought up.
SUBDIRS = [
    "libc/string",
    "libc/ctype",
    "libc/errno",
]

# Files in the globbed subdirs we do not want (see the notes).
EXCLUDE = set([
])


def sources():
    out = []
    for sub in SUBDIRS:
        for f in sorted(glob.glob(os.path.join(PICO, sub, "*.c"))):
            if os.path.basename(f) in EXCLUDE:
                continue
            out.append(f)
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
        obj = os.path.join(objdir, os.path.splitext(os.path.basename(src))[0] + ".o")
        cmd = [CLANG] + FLAGS + INCLUDES + ["-c", src, "-o", obj]
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

    if os.path.exists(args.out):
        os.remove(args.out)
    r = subprocess.run([AR, "rcs", args.out] + objs, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("archive failed:\n" + r.stderr)
    print("wrote %s: %d objects" % (os.path.relpath(args.out, ROOT), len(objs)))


if __name__ == "__main__":
    main()
