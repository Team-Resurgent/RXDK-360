#!/usr/bin/env python3
# 2026 - Team Resurgent
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of RXDK - see LICENSE.md for the full GNU GPL v3.
"""Build the standalone XUI repro (tests/gfx/xuitest.cpp).

A minimal code-driven XUI immediate-mode text demo: share our D3D device with
the XUI render library, register a TTF typeface, create a font and draw text.
Used to exercise/diagnose XUI (xuirun + xuirender) under our toolchain.

  our clang compiles xuitest.cpp against the stock XDK headers, mktitle links
  d3d9/d3dx9/xgraphics/xaudio2/xmcore (XUI render pulls in XAudio2 + XLFQueue)
  plus xuirun/xuirender, and the Arial Unicode TTF is staged next to the xex.

    xenia_canary.exe build/xuitest/xuitest.xex --headless=true
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_XDK = r"C:\Program Files (x86)\Microsoft Xbox 360 SDK"
TRIPLE = "powerpc-unknown-xbox360"

CFLAGS = [
    "-std=c++11", "-O2", "-fms-extensions", "-fms-compatibility", "-fdeclspec",
    "-fno-exceptions", "-fno-rtti",
    # Xbox WCHAR is 16-bit UTF-16; match it so L"..." typeface locators are read
    # correctly by XUI (see build_dash.py).
    "-fshort-wchar",
    "-D_WIN32=1", "-D_M_PPCBE=1", "-D_M_PPC=1", "-D_XBOX=1", "-D_XBOX_VER=200",
    "-D__export=", "-D_SIZE_T_DEFINED", "-D_XM_NO_INTRINSICS_", "-Wno-pragma-pack",
    "-Wno-invalid-token-paste",
]


def run(cmd):
    print("+", " ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stdout.write(r.stdout); sys.stderr.write(r.stderr)
        sys.exit(f"command failed ({r.returncode})")
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--xdk", default=DEFAULT_XDK)
    ap.add_argument("--clang", default=os.path.join(ROOT, "build", "llvm", "bin", "clang++.exe"))
    ap.add_argument("--outdir", default=os.path.join(ROOT, "build", "xuitest"))
    args = ap.parse_args()

    xdk_inc = os.path.join(args.xdk, "include", "xbox")
    os.makedirs(args.outdir, exist_ok=True)

    # Stage the TTF the XUI typeface descriptor points at (game:\Media\Xui\...).
    # Comes from the user's own XDK sample media -- nothing proprietary committed.
    src = os.path.join(args.xdk, "Source", "Samples", "Media", "Xui", "xarialuni.ttf")
    dstd = os.path.join(args.outdir, "Media", "Xui")
    os.makedirs(dstd, exist_ok=True)
    if os.path.exists(src):
        shutil.copyfile(src, os.path.join(dstd, "xarialuni.ttf"))
        print("staged Media/Xui/xarialuni.ttf")
    else:
        print("WARNING: font not found:", src)

    obj = os.path.join(args.outdir, "xuitest.o")
    run([args.clang, "--target=" + TRIPLE] + CFLAGS +
        ["-I", xdk_inc, "-I", HERE, "-c", os.path.join(HERE, "xuitest.cpp"), "-o", obj])

    xex = os.path.join(args.outdir, "xuitest.xex")
    run([sys.executable, os.path.join(ROOT, "tools", "mktitle.py"), obj,
         "--lib", "d3d9,d3dx9,xgraphics,xaudio2,xmcore,xuirun,xuirender",
         "--coff-dir", os.path.join(ROOT, "build", "coff"),
         "--xdk", os.path.join(args.xdk, "lib", "xbox"), "-o", xex])
    print("\nbuilt", xex)


if __name__ == "__main__":
    main()
