#!/usr/bin/env python3
"""Build and run the RXDK-360 modern-runtime C/C++ standard-library test suite.

Each test lives in tests/stdlib/t_<name>.{c,cpp} with a normal `int main()` and
uses tests/stdlib/rxdk_test.h to emit one line per assertion, which this runner
renders as a tree under each section -- so you see exactly what every section
checks, not just a single global pass:

    == string ==  (10/10)
      ok   default construct empty
      ok   append + size
      FAIL substr (got 'ell' want 'el')
      ...

COMBINED MODE (default): all sections are linked into ONE title and run in a
SINGLE xenia launch. Each test keeps its own `int main()`; the runner compiles a
per-test shim that renames it (`#define main t_<name>`) so the tests become
ordinary functions, and a generated driver calls each in turn, bracketing it
with a `[T] SECT <name>` marker. One process, one launch -- no repeated xenia
windows. A section that faults is pinned exactly (its SECT with no DONE).

ISOLATED MODE (--isolate): each test is its own title, run headless in its own
xenia. Slower (one launch per section) but a crash cannot take the others down;
useful to bisect a fault the combined run attributes to one section.

Builds with mktitle --cc clang (which auto-links the modern runtime), parses the
[T] SECT/PASS/FAIL/DONE lines from xenia.log.

  python tools/run_stdlib_tests.py                 # all sections, one launch
  python tools/run_stdlib_tests.py --isolate       # one launch per section
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

# Any [T] line, whatever xenia prefixes it with (e.g. "(DbgPrint) ").
T_RE = re.compile(r"\[T\]\s+(SECT|PASS|FAIL|DONE|ALLDONE)\b\s*(.*)")


def short(name):
    """t_string_view -> string_view: the section name the tests print."""
    return name[2:] if name.startswith("t_") else name


def find_tests(only):
    out = []
    for src in sorted(glob.glob(os.path.join(TESTS, "*.c")) +
                      glob.glob(os.path.join(TESTS, "*.cpp"))):
        name = os.path.splitext(os.path.basename(src))[0]
        if only and name not in only and short(name) not in only:
            continue
        out.append((name, src))
    return out


# ---------------------------------------------------------------- xenia run

def kill_stale_xenia():
    """Kill any lingering xenia. A xenia left running (e.g. a title that faulted
    and did not exit, or an interrupted earlier run) keeps xenia.log open, so the
    next run's log read comes back stale/empty and the fresh launch is the second
    window. Clearing them first makes every run read its own log."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/IM", "xenia_canary.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run_xenia(xex, timeout=60):
    """Launch one title headless, return the parsed [T] events (retrying on a
    dropped last line -- xenia's async logger can lose it on a fast exit)."""
    for _ in range(4):
        kill_stale_xenia()
        try:
            os.remove(XENIA_LOG)
        except OSError:
            pass
        try:
            subprocess.run([XENIA, xex, "--headless=true"], cwd=XENIA_CWD,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=timeout)
        except subprocess.TimeoutExpired:
            pass
        kill_stale_xenia()   # a faulted title can leave xenia holding the log
        events = []
        try:
            with open(XENIA_LOG, "r", encoding="utf-8", errors="replace") as f:
                for ln in f:
                    m = T_RE.search(ln)
                    if m:
                        events.append((m.group(1), m.group(2).rstrip()))
        except OSError:
            pass
        # a run that reached the end (a DONE in isolated, ALLDONE in combined)
        # is complete; otherwise retry.
        if any(k in ("DONE", "ALLDONE") for k, _ in events):
            return events
    return events


# ---------------------------------------------------------------- combined

def gen_combined(tests, workdir):
    """Emit a per-test shim (renames its main) and a driver that calls each in
    turn. Returns (sources, ordered_short_names)."""
    comb = os.path.join(workdir, "comb")
    os.makedirs(comb, exist_ok=True)
    sources, entries = [], []
    for name, src in tests:
        ext = os.path.splitext(src)[1].lower()
        is_cpp = ext != ".c"
        head = open(src, encoding="utf-8").read()
        takes_args = bool(re.search(r"int\s+main\s*\(\s*int", head))
        shim = os.path.join(comb, "shim_" + name + ext)
        with open(shim, "w", newline="\n") as f:
            # rename the test's main to t_<name>, then pull it in verbatim.
            f.write('#define main %s\n#include "%s"\n#undef main\n'
                    % (name, src.replace("\\", "/")))
        sources.append(shim)
        entries.append((name, is_cpp, takes_args))

    driver = os.path.join(comb, "driver.cpp")
    with open(driver, "w", newline="\n") as f:
        f.write("/* generated by run_stdlib_tests.py -- combined single-launch "
                "suite driver */\n")
        f.write('extern "C" int DbgPrint(const char *fmt, ...);\n')
        for name, is_cpp, takes_args in entries:
            proto = "int, char **" if takes_args else "void"
            decl = "int %s(%s);" % (name, proto)
            f.write(decl if is_cpp else 'extern "C" %s' % decl)
            f.write("\n")
        f.write("int main(int argc, char **argv) {\n")
        for name, is_cpp, takes_args in entries:
            f.write('    DbgPrint("[T] SECT %s\\n");\n' % short(name))
            call = "%s(argc, argv)" % name if takes_args else "%s()" % name
            f.write("    %s;\n" % call)
        f.write('    DbgPrint("[T] ALLDONE\\n");\n')
        f.write("    return 0;\n}\n")
    sources.append(driver)
    return sources, [short(n) for n, _, _ in entries]


def build_combined(sources, workdir):
    out = os.path.join(workdir, "suite.xex")
    cmd = [sys.executable, os.path.join(HERE, "mktitle.py")] + sources + \
          ["-o", out, "--cc", "clang", "--cflag=-I" + TESTS]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        return None, (r.stderr or r.stdout).strip()
    return out, None


def parse_combined(events, ordered):
    """Fold the flat event stream into per-section {checks, done}. A section
    started (SECT) but not closed (DONE) before the next SECT/ALLDONE faulted."""
    sections = {n: {"checks": [], "started": False, "done": False}
                for n in ordered}
    cur = None
    alldone = False
    for kind, text in events:
        if kind == "SECT":
            cur = text.strip()
            if cur in sections:
                sections[cur]["started"] = True
        elif kind in ("PASS", "FAIL"):
            if cur in sections:
                sections[cur]["checks"].append((kind, text))
        elif kind == "DONE":
            sec = text.split()[0] if text else cur
            if sec in sections:
                sections[sec]["done"] = True
        elif kind == "ALLDONE":
            alldone = True
    return sections, alldone


# ---------------------------------------------------------------- isolated

def build_isolated(name, src, workdir):
    out = os.path.join(workdir, name + ".xex")
    cmd = [sys.executable, os.path.join(HERE, "mktitle.py"), src, "-o", out,
           "--cc", "clang", "--cflag=-I" + TESTS]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        tail = (r.stderr or r.stdout).strip().splitlines()[-4:]
        return None, "\n".join(tail)
    return out, None


# ---------------------------------------------------------------- render

def render_section(name, checks, complete):
    npass = sum(1 for k, _ in checks if k == "PASS")
    nfail = sum(1 for k, _ in checks if k == "FAIL")
    n = npass + nfail
    status = "" if complete else "  DID NOT COMPLETE (crash/timeout)"
    print("== %s ==  (%d/%d)%s" % (name, npass, n, status))
    for kind, text in checks:
        print("     %-4s %s" % ("ok" if kind == "PASS" else "FAIL", text))
    return npass, n, (complete and nfail == 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="", help="comma-separated section names")
    ap.add_argument("--list", action="store_true", help="list sections and exit")
    ap.add_argument("--isolate", action="store_true",
                    help="one xenia launch per section (default: one launch for "
                         "the whole suite)")
    args = ap.parse_args()

    only = set(n for n in args.only.split(",") if n)
    tests = find_tests(only)
    if args.list:
        for name, _ in tests:
            print(short(name))
        return 0
    os.makedirs(BUILD, exist_ok=True)

    total_pass = total_checks = 0
    bad_sections = 0

    if args.isolate:
        for name, src in tests:
            xex, err = build_isolated(name, src, BUILD)
            if not xex:
                print("== %s ==  BUILD FAILED" % short(name))
                for l in err.splitlines():
                    print("     " + l)
                bad_sections += 1
                continue
            events = run_xenia(xex)
            checks = [(k, t) for k, t in events if k in ("PASS", "FAIL")]
            complete = any(k == "DONE" for k, _ in events)
            p, n, clean = render_section(short(name), checks, complete)
            total_pass += p
            total_checks += n
            if not clean:
                bad_sections += 1
    else:
        sources, ordered = gen_combined(tests, BUILD)
        xex, err = build_combined(sources, BUILD)
        if not xex:
            print("== SUITE ==  BUILD FAILED")
            for l in err.splitlines()[-30:]:
                print("     " + l)
            return 1
        events = run_xenia(xex, timeout=120)
        sections, alldone = parse_combined(events, ordered)
        for name in ordered:
            s = sections[name]
            complete = s["done"]
            p, n, clean = render_section(name, s["checks"], complete)
            total_pass += p
            total_checks += n
            if not clean:
                bad_sections += 1
        if not alldone:
            print("\n(note: suite did not reach ALLDONE -- the last section "
                  "shown without a completion likely faulted)")

    print("")
    print("SUMMARY: %d/%d sections clean, %d/%d checks passed"
          % (len(tests) - bad_sections, len(tests), total_pass, total_checks))
    return 1 if bad_sections else 0


if __name__ == "__main__":
    sys.exit(main())
