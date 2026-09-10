#!/usr/bin/env python3
"""Build the four drop-in CRT variants that mirror the official XDK lib names, so
projects that link libcMT/libcMTd/libcpMT/libcpMTd resolve against our modern
runtime instead of the Microsoft CRT:

    libcMT.a    C runtime, release   (our libc.a  -- build_libc.py)
    libcMTd.a   C runtime, _DEBUG    (build_libc.py  --debug)
    libcpMT.a   C++ runtime, release (our libcpp.a -- build_libcpp.py)
    libcpMTd.a  C++ runtime, _DEBUG  (build_libcpp.py --debug)

The C runtime (libcMT) carries the C library plus operator new/delete and the MS
CRT compatibility surface (ms_crt_compat.c / cxxrt.cpp aliases); the C++ runtime
(libcpMT) carries the exception/RTTI/STL machinery (libunwind + libc++abi +
libc++). Debug variants define _DEBUG, build -O0 -g and leave assertions active.

    python tools/build_variants.py            build all four into build/variants/
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "build", "variants")

# (name, builder script, extra args) -- each builds in its own dir so the
# per-build objdir (dirname(out)/obj) never mixes release and debug objects.
VARIANTS = [
    ("libcMT.a",   "build_libc.py",   []),
    ("libcMTd.a",  "build_libc.py",   ["--debug"]),
    ("libcpMT.a",  "build_libcpp.py", []),
    ("libcpMTd.a", "build_libcpp.py", ["--debug"]),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    made = []
    for name, script, extra in VARIANTS:
        workdir = os.path.join(OUT, os.path.splitext(name)[0])
        os.makedirs(workdir, exist_ok=True)
        out = os.path.join(workdir, name)
        cmd = [sys.executable, os.path.join(HERE, script), "-o", out] + extra
        print("==> %s (%s %s)" % (name, script, " ".join(extra) or "release"))
        r = subprocess.run(cmd)
        if r.returncode != 0 or not os.path.exists(out):
            sys.exit("  build failed: %s" % name)
        final = os.path.join(OUT, name)
        shutil.copyfile(out, final)
        made.append((name, os.path.getsize(final)))

    print("\nwrote %d variants to %s:" % (len(made), os.path.relpath(OUT, ROOT)))
    for name, size in made:
        print("  %-12s %.1f MB" % (name, size / (1024 * 1024)))


if __name__ == "__main__":
    main()
