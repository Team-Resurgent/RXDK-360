#!/usr/bin/env python3
"""Bulk regression harness: build each corpus title, run it in xenia, diff output.

Each tests/corpus/<name>/ holds main.c or main.cpp and expected.txt (the lines it
should print through DbgPrint). Optionally build.args -- extra mktitle arguments,
whitespace-separated. For each program the harness:

  1. builds it via mktitle (the program + the shared _prelude/start.c), linking
     the current runtime (the translated libcMT by default),
  2. runs it headless in xenia,
  3. extracts the (DbgPrint) lines from the log and diffs them against expected.

The point is to freeze the *current* behaviour as golden -- including the MS-CRT
quirks titles rely on (printf %S = wide string, etc.) -- so that swapping in the
modern C23/C++23 runtime later surfaces any divergence as a failing test.

  python tools/run_corpus.py                 # run + report
  python tools/run_corpus.py --bless         # (re)write expected.txt from this run
  python tools/run_corpus.py --only hello,vcall
  python tools/run_corpus.py --lib libcMT,xapilib   # link a different runtime

Env overrides: RXDK_XENIA, RXDK_XENIA_CWD, RXDK_XENIA_LOG.
"""
import argparse
import glob
import os
import re
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CORPUS = os.path.join(ROOT, "tests", "corpus")
PRELUDE = os.path.join(CORPUS, "_prelude")
BUILD = os.path.join(ROOT, "build", "corpus")

XENIA = os.environ.get(
    "RXDK_XENIA",
    r"D:\Git\xenia-canary\build\bin\Windows\Release\xenia_canary.exe")
XENIA_CWD = os.environ.get("RXDK_XENIA_CWD", r"D:\Git\xenia_canary_windows")
XENIA_LOG = os.environ.get(
    "RXDK_XENIA_LOG",
    r"D:\Git\xenia-canary\build\bin\Windows\Release\xenia.log")

DBG_RE = re.compile(r"\(DbgPrint\)\s?(.*)")
SENTINEL = "##RXDK-CORPUS-END##"       # start.c prints this last; marks a complete run


def programs(only):
    for d in sorted(glob.glob(os.path.join(CORPUS, "*"))):
        name = os.path.basename(d)
        if name == "_prelude" or not os.path.isdir(d):
            continue
        if only and name not in only:
            continue
        src = None
        for cand in ("main.cpp", "main.c"):
            if os.path.exists(os.path.join(d, cand)):
                src = os.path.join(d, cand)
        if src:
            yield name, d, src


def build(name, srcdir, src, libs):
    out = os.path.join(BUILD, name, name + ".xex")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    extra = []
    argfile = os.path.join(srcdir, "build.args")
    if os.path.exists(argfile):
        extra = shlex.split(open(argfile).read())
    cmd = [sys.executable, os.path.join(HERE, "mktitle.py"),
           src, os.path.join(PRELUDE, "start.c"), "-o", out,
           "--cc", "clang", "--cflag=-I" + PRELUDE]
    for lib in libs:
        cmd += ["--lib", lib]
    cmd += extra
    r = subprocess.run(cmd, capture_output=True, text=True)
    return out, r


def _one_run(xex):
    try:
        os.remove(XENIA_LOG)
    except OSError:
        pass
    try:
        subprocess.run([XENIA, xex, "--headless=true"], cwd=XENIA_CWD,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=25)
    except subprocess.TimeoutExpired:
        pass
    lines, complete = [], False
    try:
        with open(XENIA_LOG, "r", encoding="utf-8", errors="replace") as f:
            for ln in f:
                m = DBG_RE.search(ln)
                if not m:
                    continue
                text = m.group(1).rstrip()
                if text == SENTINEL:
                    complete = True          # the title reached the end cleanly
                else:
                    lines.append(text)
    except OSError:
        pass
    return lines, complete


def run_in_xenia(xex, attempts=3):
    """Run the title, returning its DbgPrint lines. Retries when the completion
    sentinel never arrived (a truncated log), so the async-logger flush race does
    not show up as a spurious mismatch."""
    lines = []
    for _ in range(attempts):
        lines, complete = _one_run(xex)
        if complete:
            break
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bless", action="store_true",
                    help="write expected.txt from this run instead of diffing")
    ap.add_argument("--only", default="", help="comma-separated program names")
    ap.add_argument("--lib", default="libcMT",
                    help="comma-separated runtime libraries to link (default libcMT)")
    args = ap.parse_args()

    only = set(n for n in args.only.split(",") if n)
    libs = [n for n in args.lib.split(",") if n]

    rows = []
    fails = 0
    pend = 0
    for name, srcdir, src in programs(only):
        # a program with a `pending` file is a known-blocked probe (e.g. it needs
        # the modern runtime); its build failure does not fail the run.
        pending = os.path.exists(os.path.join(srcdir, "pending"))
        xex, br = build(name, srcdir, src, libs)
        if br.returncode != 0 or not os.path.exists(xex):
            if pending:
                reason = open(os.path.join(srcdir, "pending")).read().strip()
                rows.append((name, "PENDING", [reason] if reason else []))
                pend += 1
            else:
                rows.append((name, "BUILD-FAIL", (br.stderr or br.stdout).strip().splitlines()[-1:] or [""]))
                fails += 1
            continue
        if pending:
            rows.append((name, "PENDING?", ["builds now -- remove the pending marker and bless"]))
            pend += 1
            continue
        actual = run_in_xenia(xex)
        exp_path = os.path.join(srcdir, "expected.txt")
        if args.bless:
            with open(exp_path, "w", newline="\n") as f:
                f.write("\n".join(actual) + ("\n" if actual else ""))
            rows.append((name, "BLESSED", ["%d line(s)" % len(actual)]))
            continue
        expected = []
        if os.path.exists(exp_path):
            expected = open(exp_path).read().splitlines()
        if actual == expected:
            rows.append((name, "PASS", []))
        else:
            fails += 1
            diff = []
            for i in range(max(len(actual), len(expected))):
                a = actual[i] if i < len(actual) else "<none>"
                e = expected[i] if i < len(expected) else "<none>"
                if a != e:
                    diff.append("  exp: %s" % e)
                    diff.append("  got: %s" % a)
            rows.append((name, "MISMATCH", diff))

    width = max(len(n) for n, _, _ in rows) if rows else 4
    print()
    for name, status, detail in rows:
        print("  %-*s  %s" % (width, name, status))
        for d in detail:
            print("      " + d)
    total = len(rows)
    if args.bless:
        print("\n%d program(s) blessed." % total)
        return 0
    passed = total - fails - pend
    tail = (", %d pending" % pend) if pend else ""
    print("\n%d/%d passed, %d failed%s." % (passed, total, fails, tail))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
