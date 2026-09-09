#!/usr/bin/env python3
"""Build and run the RXDK-360 modern-runtime C/C++ standard-library test suite.

Each test in tests/stdlib/<name>.{c,cpp} is its own title (isolated, so one
crash does not take the others down). It uses tests/stdlib/rxdk_test.h to emit a
line per assertion, which this runner renders as a tree under each section, so
you can see exactly what every section checks -- not just a single global pass:

    == string ==  (10/10)
      ok   default construct empty
      ok   append + size
      FAIL substr (got 'ell' want 'el')
      ...

Builds with mktitle --cc clang (which auto-links the modern runtime), runs each
headless in xenia, parses the [T] PASS/FAIL/DONE lines. A missing DONE line means
the title crashed or hung before finishing -- reported distinctly from failures.

  python tools/run_stdlib_tests.py                 # all sections
  python tools/run_stdlib_tests.py --only file_io,string
  python tools/run_stdlib_tests.py --list
"""
import argparse
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TESTS = os.path.join(ROOT, "tests", "stdlib")
BUILD = os.path.join(ROOT, "build", "stdlib")

XENIA = os.environ.get(
    "RXDK_XENIA", r"D:\Git\xenia-canary\build\bin\Windows\Release\xenia_canary.exe")
XENIA_CWD = os.environ.get("RXDK_XENIA_CWD", r"D:\Git\xenia_canary_windows")
XENIA_LOG = os.environ.get(
    "RXDK_XENIA_LOG", r"D:\Git\xenia-canary\build\bin\Windows\Release\xenia.log")

LINE_RE = re.compile(r"\(DbgPrint\)\s*\[T\]\s+(PASS|FAIL|DONE)\s+(.*)")


def find_tests(only):
    out = []
    for src in sorted(glob.glob(os.path.join(TESTS, "*.c")) +
                      glob.glob(os.path.join(TESTS, "*.cpp"))):
        name = os.path.splitext(os.path.basename(src))[0]
        if only and name not in only:
            continue
        out.append((name, src))
    return out


def build(name, src):
    out = os.path.join(BUILD, name + ".xex")
    cmd = [sys.executable, os.path.join(HERE, "mktitle.py"), src, "-o", out,
           "--cc", "clang", "--cflag=-I" + TESTS]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        tail = (r.stderr or r.stdout).strip().splitlines()[-4:]
        return None, "\n".join(tail)
    return out, None


def run_once(xex):
    try:
        os.remove(XENIA_LOG)
    except OSError:
        pass
    try:
        subprocess.run([XENIA, xex, "--headless=true"], cwd=XENIA_CWD,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
    except subprocess.TimeoutExpired:
        pass
    checks, done = [], None
    try:
        with open(XENIA_LOG, "r", encoding="utf-8", errors="replace") as f:
            for ln in f:
                m = LINE_RE.search(ln)
                if not m:
                    continue
                kind, text = m.group(1), m.group(2).rstrip()
                if kind == "DONE":
                    done = text
                else:
                    checks.append((kind, text))
    except OSError:
        pass
    return checks, done


def run(xex):
    # retry a few times: xenia's async logger can drop the last line on a fast
    # exit, so treat a run with no DONE as incomplete and try again.
    for _ in range(4):
        checks, done = run_once(xex)
        if done is not None:
            return checks, done
    return checks, done


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="", help="comma-separated section names")
    ap.add_argument("--list", action="store_true", help="list sections and exit")
    args = ap.parse_args()

    only = set(n for n in args.only.split(",") if n)
    tests = find_tests(only)
    if args.list:
        for name, _ in tests:
            print(name)
        return 0
    os.makedirs(BUILD, exist_ok=True)

    total_pass = total_checks = 0
    bad_sections = 0

    for name, src in tests:
        xex, err = build(name, src)
        if not xex:
            print("== %s ==  BUILD FAILED" % name)
            for l in err.splitlines():
                print("     " + l)
            bad_sections += 1
            continue

        checks, done = run(xex)
        npass = sum(1 for k, _ in checks if k == "PASS")
        nfail = sum(1 for k, _ in checks if k == "FAIL")
        n = npass + nfail

        status = ""
        if done is None:
            status = "  DID NOT COMPLETE (crash/timeout)"
            bad_sections += 1
        elif nfail:
            bad_sections += 1
        print("== %s ==  (%d/%d)%s" % (name, npass, n, status))
        for kind, text in checks:
            print("     %-4s %s" % ("ok" if kind == "PASS" else "FAIL", text))

        total_pass += npass
        total_checks += n

    print("")
    print("SUMMARY: %d/%d sections clean, %d/%d checks passed"
          % (len(tests) - bad_sections, len(tests), total_pass, total_checks))
    return 1 if bad_sections else 0


if __name__ == "__main__":
    sys.exit(main())
