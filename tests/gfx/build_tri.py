#!/usr/bin/env python3
# 2026 - Team Resurgent
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of RXDK - see LICENSE.md for the full GNU GPL v3.
"""Build the spinning-triangle bring-up (tests/gfx/tri.cpp) end to end.

Reproducible recipe for the D3D9 triangle milestone:

  1. compile the two HLSL shaders to Xbox 360 GPU microcode with the XDK's
     fxc.exe (offline -- we deliberately avoid the runtime D3DXCompileShader
     HLSL compiler, which is huge and hangs under xenia's JIT; shipping titles
     precompile their shaders too),
  2. compile tri.cpp with our patched clang against the stock XDK D3D9 headers
     (D3DX matrix math, xnamath in scalar mode -- no VMX128 intrinsics),
  3. link + pack into a XEX with mktitle.py (our ld.lld + elf2xex).

The XDK is the user's own install; nothing from it is redistributed. Point
--xdk at it (default: the standard install path). Run xenia on the result:

    xenia_canary.exe build/tri/tri.xex --headless=true
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

DEFAULT_XDK = r"C:\Program Files (x86)\Microsoft Xbox 360 SDK"
TRIPLE = "powerpc-unknown-xbox360"

# The stock XDK headers compile with our clang given this MS-compat recipe; see
# docs/sdk-headers-plan.md. -D_XM_NO_INTRINSICS_ puts xnamath/xboxmath in scalar
# mode so d3dx9math.h / xgraphics.h compile without VMX128 intrinsics.
CFLAGS = [
    "-std=c++11", "-O2",
    "-fms-extensions", "-fms-compatibility", "-fdeclspec",
    "-fno-exceptions", "-fno-rtti",
    "-D_WIN32=1", "-D_M_PPCBE=1", "-D_M_PPC=1", "-D_XBOX=1", "-D_XBOX_VER=200",
    "-D__export=", "-D_SIZE_T_DEFINED", "-D_XM_NO_INTRINSICS_",
    "-Wno-pragma-pack",
]

SHADERS = [
    ("tri_vs.hlsl", "tri_vs.h", "vs_3_0", "g_vs_bin"),
    ("tri_ps.hlsl", "tri_ps.h", "ps_3_0", "g_ps_bin"),
]


def run(cmd):
    print("+", " ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
        sys.exit(f"command failed ({r.returncode})")
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--xdk", default=DEFAULT_XDK, help="Xbox 360 SDK install root")
    ap.add_argument("--clang", default=os.path.join(ROOT, "build", "llvm", "bin", "clang++.exe"))
    ap.add_argument("--fxc", help="fxc.exe (default: <xdk>/bin/win32/fxc.exe)")
    ap.add_argument("--outdir", default=os.path.join(ROOT, "build", "tri"))
    ap.add_argument("--spin", action="store_true",
                    help="build the windowed 'spin forever' variant for screenshots")
    args = ap.parse_args()

    fxc = args.fxc or os.path.join(args.xdk, "bin", "win32", "fxc.exe")
    xdk_inc = os.path.join(args.xdk, "include", "xbox")
    shdir = os.path.join(HERE, "shaders")
    os.makedirs(args.outdir, exist_ok=True)

    # 1. shaders -> microcode headers (checked in, but regenerated here so the
    #    recipe is self-contained and the headers stay in sync with the HLSL).
    for src, hdr, profile, var in SHADERS:
        run([fxc, "/nologo", "/T", profile, "/E", "main",
             "/Fh", os.path.join(shdir, hdr), "/Vn", var,
             os.path.join(shdir, src)])

    # 2. compile tri.cpp
    obj = os.path.join(args.outdir, "tri_spin.o" if args.spin else "tri.o")
    cflags = list(CFLAGS) + (["-DTRI_SPIN_FOREVER"] if args.spin else [])
    run([args.clang, "--target=" + TRIPLE] + cflags +
        ["-I", xdk_inc, "-I", HERE, "-c", os.path.join(HERE, "tri.cpp"), "-o", obj])

    # 3. link + pack
    xex = os.path.join(args.outdir, "tri_spin.xex" if args.spin else "tri.xex")
    run([sys.executable, os.path.join(ROOT, "tools", "mktitle.py"), obj,
         "--lib", "d3d9,d3dx9,xgraphics",
         "--coff-dir", os.path.join(ROOT, "build", "coff"),
         "--xdk", os.path.join(args.xdk, "lib", "xbox"), "-o", xex])
    print("\nbuilt", xex)


if __name__ == "__main__":
    main()
