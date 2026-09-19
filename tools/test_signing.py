#!/usr/bin/env python3
"""Regression guard for issue #2 (mktitle produced an unsigned, unloadable XEX).

mktitle/elf2xex used to leave the XEX signature zeroed; a real kit classifies a
zero-signature image as retail and rejects it (LDRX C000007B), while the VS build
path debug-signed it -- so the two build paths disagreed and a mktitle-built
title could not load. elf2xex now debug-signs by default (tools/xex_debugsign.py).

This builds a tiny title through the real mktitle path and asserts:
  * the default output is validly debug-signed (verify_signed passes), and
  * --no-sign produces an image verify_signed rejects as unsigned,
so a regression that stops signing, or breaks the signer, fails here.

    python tools/test_signing.py
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import xex_debugsign  # noqa: E402

PRELUDE = os.path.join(ROOT, "tests", "corpus", "_prelude")
LIBS = [os.path.join(ROOT, "build", "libc", "libcpp.a"),
        os.path.join(ROOT, "build", "libc", "libc.a"),
        os.path.join(ROOT, "build", "coff", "xapilib.a")]

# a minimal title: prints one line, then the corpus start.c powers off. The
# import machinery (DbgPrint is a kernel import) exercises the import-hash stage.
SRC = r'''
#include "rt.h"
void title_main(void) { DbgPrint("signing test title\n"); }
'''


def mktitle(src, out, sign):
    cmd = [sys.executable, os.path.join(HERE, "mktitle.py"),
           src, os.path.join(PRELUDE, "start.c"),
           "-o", out, "--cc", "clang", "--no-default-libs",
           "--cflag=-I" + PRELUDE]
    for lib in LIBS:
        cmd += ["--lib", lib]
    if not sign:
        cmd += ["--no-sign"]
    return subprocess.run(cmd, capture_output=True, text=True)


def main():
    for lib in LIBS:
        if not os.path.exists(lib):
            print("SKIP: runtime not built (%s missing)" % os.path.relpath(lib, ROOT))
            return 0
    tmp = tempfile.mkdtemp(prefix="rxdk_signtest_")
    src = os.path.join(tmp, "t.c")
    open(src, "w").write(SRC)
    fails = 0

    signed = os.path.join(tmp, "signed.xex")
    r = mktitle(src, signed, sign=True)
    if r.returncode != 0 or not os.path.exists(signed):
        print("FAIL: signed build failed:\n" + (r.stderr or r.stdout)[-400:])
        return 1
    problems = xex_debugsign.verify_signed(open(signed, "rb").read())
    if problems:
        print("FAIL: default mktitle output is not validly signed: " + "; ".join(problems))
        fails += 1
    else:
        print("PASS: default mktitle output is validly debug-signed")

    unsigned = os.path.join(tmp, "unsigned.xex")
    r = mktitle(src, unsigned, sign=False)
    if r.returncode != 0 or not os.path.exists(unsigned):
        print("FAIL: --no-sign build failed:\n" + (r.stderr or r.stdout)[-400:])
        return 1
    problems = xex_debugsign.verify_signed(open(unsigned, "rb").read())
    if not problems:
        print("FAIL: --no-sign output was accepted as signed (verifier is blind)")
        fails += 1
    else:
        print("PASS: --no-sign output correctly rejected (%s)" % problems[0])

    print("\n%s" % ("FAILED" if fails else "OK: signing regression intact"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
