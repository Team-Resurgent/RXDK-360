#!/usr/bin/env python3
"""Regression guard for issue #3 (mktitle link failures on real XDK code).

Two link failures used to hit any title built with mktitle that pulls in real
XDK code, both already handled in the VS path (ClangLink.cs) but missing from
the script:

  1. "unable to find library from dependent library specifier: libcpmt" -- XDK
     headers (use_ansi.h et al) bake #pragma comment(lib, ...) into every object
     that includes them, which clang emits as a .deplibs record; ld.lld follows
     it and searches for MSVC's C++ static runtime, which the modern toolchain
     does not ship. Fixed by mktitle passing --no-dependent-libraries.
  2. "duplicate symbol: atan2f" (and similar) -- XDK static libs bundle their own
     CRT pieces (xaudio2.lib carries its own atan2f) that collide with the modern
     runtime's. Handled by link order (title/lib objects resolve ties first).

This links one title that triggers BOTH at once -- it bakes the libcpmt deplib
and references an xaudio2 export -- and asserts a clean link. It fails if either
fix regresses. (The broader per-lib link+run coverage lives in lib_titletest.py.)

    python tools/test_xdk_link.py
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

COFF = os.path.join(ROOT, "build", "coff")
RUNTIME = [os.path.join(ROOT, "build", "libc", "libcpp.a"),
           os.path.join(ROOT, "build", "libc", "libc.a"),
           os.path.join(ROOT, "build", "coff", "xapilib.a")]
# xaudio2 + its sibling dep (XLFQueue* live in xmcore), the atan2f-dup case.
XDK_LIBS = [os.path.join(COFF, "xaudio2.a"), os.path.join(COFF, "xmcore.a")]

SRC = r'''
#pragma comment(lib, "libcpmt")   /* what XDK headers (use_ansi.h et al) bake in */
extern int DbgPrint(const char *, ...);
extern void XAudio2Create(void);  /* pulls xaudio2.a, which carries its own atan2f */
void *volatile sink;
void title_main(void) { sink = (void *)&XAudio2Create; DbgPrint("xdk link ok\n"); }
'''


def main():
    need = RUNTIME + XDK_LIBS
    missing = [p for p in need if not os.path.exists(p)]
    if missing:
        print("SKIP: not built: " + ", ".join(os.path.relpath(p, ROOT) for p in missing))
        return 0
    tmp = tempfile.mkdtemp(prefix="rxdk_xdklink_")
    src = os.path.join(tmp, "t.c")
    open(src, "w").write(SRC)
    out = os.path.join(tmp, "t.xex")
    prelude = os.path.join(ROOT, "tests", "corpus", "_prelude")
    cmd = [sys.executable, os.path.join(HERE, "mktitle.py"),
           src, os.path.join(prelude, "start.c"), "-o", out,
           "--cc", "clang", "--no-default-libs", "--cflag=-I" + prelude]
    for lib in RUNTIME + XDK_LIBS:
        cmd += ["--lib", lib]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode == 0 and os.path.exists(out):
        print("PASS: XDK-code title links (libcpmt deplib dropped, atan2f dup resolved)")
        return 0
    msg = (r.stderr or r.stdout)
    tail = [ln for ln in msg.strip().splitlines() if ln.strip()][-4:]
    print("FAIL: XDK-code title did not link:\n  " + "\n  ".join(tail))
    return 1


if __name__ == "__main__":
    sys.exit(main())
