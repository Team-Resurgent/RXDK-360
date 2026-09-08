#!/usr/bin/env python3
"""Check the patched compiler against the platform compiler.

Compiles the ABI probes with both the Xbox 360 XDK's cl.exe and the patched
clang, extracts the specific fact each probe was written to expose, and reports
agreement rule by rule. Each row corresponds to one measured property in
docs/abi-complete.md.

Usage:  RXDK_CLANG=<path to patched clang> python tools/verify_abi.py
"""
import os, re, subprocess, sys, shutil, tempfile

XEDK = os.environ.get("XEDK", r"C:\Program Files (x86)\Microsoft Xbox 360 SDK")
CL = os.path.join(XEDK, "bin", "win32", "cl.exe")
DUMPBIN = os.path.join(XEDK, "bin", "win32", "dumpbin.exe")
CLANG = os.environ.get("RXDK_CLANG", r"D:\Git\RXDK-360\build\llvm\bin\clang.exe")
TRIPLE = os.environ.get("RXDK_TRIPLE", "powerpc-unknown-xbox360")
SPIKE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "spike")


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def ms_listing(src, workdir):
    obj = os.path.join(workdir, os.path.basename(src) + ".obj")
    r = run([CL, "-c", "-O2", "-I" + os.path.join(XEDK, "include", "xbox"),
             src, "-Fo" + obj])
    if not os.path.exists(obj):
        return None, (r.stdout + r.stderr).strip()
    return run([DUMPBIN, "-DISASM:NOBYTES", obj]).stdout, None


def clang_listing(src, workdir):
    out = os.path.join(workdir, os.path.basename(src) + ".s")
    r = run([CLANG, "--target=" + TRIPLE, "-O2", "-maltivec", "-S", src, "-o", out])
    if not os.path.exists(out):
        return None, (r.stdout + r.stderr).strip()
    return open(out, encoding="utf-8", errors="replace").read(), None


def slice_fn(text, name):
    """Return the body of one function from either listing format."""
    m = re.search(r"^" + re.escape(name) + r":[^\n]*\n(.*?)(?=\n[A-Za-z_.][\w.@?]*:|\Z)",
                  text, re.S | re.M)
    return m.group(1) if m else ""


# ---- extractors -------------------------------------------------------------

def stack_arg_offsets(body):
    """Byte offsets of arguments spilled into the parameter save area."""
    offs = set()
    for m in re.finditer(r"st[wd]\s+r\d+,\s*([0-9A-Fa-f]+)h\(r1\)", body):   # cl.exe
        offs.add(int(m.group(1), 16))
    for m in re.finditer(r"st[wd]\s+\d+,\s*(\d+)\(1\)", body):               # clang
        offs.add(int(m.group(1)))
    return sorted(o for o in offs if o >= 0x10)


def arg_regs(body):
    """Registers that receive an argument value in these probes.

    Restricted to immediate loads and floating-point loads, so that address
    temporaries are not mistaken for argument registers.
    """
    regs = set()
    for m in re.finditer(r"li\s+r(\d+),", body):           # cl.exe integer
        regs.add(("r", int(m.group(1))))
    for m in re.finditer(r"lf[sd]\s+fr(\d+),", body):      # cl.exe float
        regs.add(("f", int(m.group(1))))
    for m in re.finditer(r"^\s*li\s+(\d+),", body, re.M):     # clang integer
        regs.add(("r", int(m.group(1))))
    for m in re.finditer(r"^\s*lf[sd]\s+(\d+),", body, re.M): # clang float
        regs.add(("f", int(m.group(1))))
    return sorted(r for r in regs
                  if (r[0] == "r" and 3 <= r[1] <= 10)
                  or (r[0] == "f" and 1 <= r[1] <= 13))


def vec_regs(body):
    regs = {int(m) for m in re.findall(r"\bvr(\d+)\b", body)}                # cl.exe
    regs |= {int(m) for m in re.findall(r"^\s*(?:vor|vmr|stvx|lvx)\s+(\d+),", body, re.M)}
    return sorted(r for r in regs if r <= 13)


def lr_slot(body):
    """Where the return address is stored, relative to the incoming r1."""
    m = re.search(r"st[wd]\s+r12,\s*-([0-9A-Fa-f]+)\(r1\)", body)            # cl.exe
    if m:
        return [-int(m.group(1), 16)]
    m = re.search(r"stw\s+0,\s*(-?\d+)\(1\)", body)                          # clang
    return [int(m.group(1))] if m else []


# ---- rules ------------------------------------------------------------------
# (label, source for cl.exe, source for clang, function, extractor)
RULES = [
    ("stack parameter slots",     "abi/abi.c",     "abi/abi.c",     "call_many",  stack_arg_offsets),
    ("mixed int/float registers", "abi/abi.c",     "abi/abi.c",     "call_mixed", arg_regs),
    ("vector argument registers", "vmx/vec2.c",    "abi2/vecn.c",   "def_mixv",   vec_regs),
    ("return address slot",       "abi/abi.c",     "abi/abi.c",     "call_many",  lr_slot),
]


def main():
    if not os.path.exists(CLANG):
        print(f"patched clang not found: {CLANG}\nrun tools/build-llvm.bat first")
        return 2
    if not os.path.exists(CL):
        print(f"XDK compiler not found: {CL}")
        return 2

    work = tempfile.mkdtemp(prefix="rxdk360-verify-")
    width = max(len(r[0]) for r in RULES)
    agree = differ = skipped = 0
    for label, msrel, crel, fn, extract in RULES:
        msrc = os.path.normpath(os.path.join(SPIKE, msrel))
        csrc = os.path.normpath(os.path.join(SPIKE, crel))
        if not (os.path.exists(msrc) and os.path.exists(csrc)):
            print(f"{label:<{width}}  SKIP  missing probe"); skipped += 1; continue
        mtext, merr = ms_listing(msrc, work)
        ctext, cerr = clang_listing(csrc, work)
        if mtext is None or ctext is None:
            print(f"{label:<{width}}  SKIP  compile failed")
            for who, err in (("cl", merr), ("clang", cerr)):
                if err:
                    print(f"      {who}: {err.splitlines()[-1][:90]}")
            skipped += 1
            continue
        a, b = extract(slice_fn(mtext, fn)), extract(slice_fn(ctext, fn))
        if not a and not b:
            print(f"{label:<{width}}  SKIP  nothing extracted (probe or pattern wrong)")
            skipped += 1
            continue
        ok = a == b
        agree += ok
        differ += not ok
        print(f"{label:<{width}}  {'OK  ' if ok else 'DIFF'}  cl={a}  clang={b}")
    print(f"\n{agree} agree, {differ} differ, {skipped} skipped")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if differ else 0


sys.exit(main())
