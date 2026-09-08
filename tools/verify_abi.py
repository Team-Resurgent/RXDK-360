#!/usr/bin/env python3
"""Check the patched compiler against the platform compiler.

Compiles the ABI probes with both the Xbox 360 XDK's cl.exe and the patched
clang, extracts the specific facts each probe was written to expose, and
reports agreement per rule. This is the acceptance test for the LLVM patch:
each row corresponds to one measured property in docs/abi-complete.md.
"""
import os, re, subprocess, sys, shutil, tempfile

XEDK = os.environ.get("XEDK", r"C:\Program Files (x86)\Microsoft Xbox 360 SDK")
CL = os.path.join(XEDK, "bin", "win32", "cl.exe")
DUMPBIN = os.path.join(XEDK, "bin", "win32", "dumpbin.exe")
CLANG = os.environ.get("RXDK_CLANG", r"D:\Git\RXDK-360\build\llvm\bin\clang.exe")
TRIPLE = "powerpc64-unknown-xbox360"
SPIKE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "spike")

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def ms_disasm(src, workdir, extra_inc=True):
    obj = os.path.join(workdir, "ms.obj")
    cmd = [CL, "-c", "-O2", src, "-Fo" + obj]
    if extra_inc:
        cmd.insert(3, "-I" + os.path.join(XEDK, "include", "xbox"))
    r = run(cmd)
    if not os.path.exists(obj):
        return None, r.stdout + r.stderr
    return run([DUMPBIN, "-DISASM:NOBYTES", obj]).stdout, None

def clang_asm(src, workdir):
    out = os.path.join(workdir, "clang.s")
    r = run([CLANG, "--target=" + TRIPLE, "-O2", "-S",
             "-I" + os.path.join(XEDK, "include", "xbox"), src, "-o", out])
    if not os.path.exists(out):
        return None, r.stdout + r.stderr
    return open(out, encoding="utf-8", errors="replace").read(), None

def func(text, name, ms):
    """Slice one function out of a listing."""
    if ms:
        m = re.search(r"^" + re.escape(name) + r":\n(.*?)(?=^\S.*:$|\Z)",
                      text, re.S | re.M)
    else:
        m = re.search(r"^" + re.escape(name) + r":.*?\n(.*?)(?=^\S.*:|\Z)",
                      text, re.S | re.M)
    return m.group(1) if m else ""

# Each rule: (label, probe file, function, extractor)
def stack_arg_offsets(body):
    """Offsets of arguments spilled to the parameter save area."""
    offs = re.findall(r"st[wd]\s+r?\d+,\s*(-?[0-9A-Fa-f]+)h?\(r1\)", body)
    offs += re.findall(r"st[wd]\s+\d+,\s*(-?\d+)\(1\)", body)
    return sorted({int(o, 16) if not o.lstrip('-').isdigit() or
                   re.search(r"[A-Fa-f]", o) else int(o) for o in offs})

def gprs_used(body):
    return sorted({int(m) for m in re.findall(r"\br(\d+)\b", body)} |
                  {int(m) for m in re.findall(r"^\s*\w+\s+(\d+),", body, re.M)})

def vec_regs(body):
    return sorted({int(m) for m in re.findall(r"\bv(?:r)?(\d+)\b", body)})

RULES = [
    ("stack parameter slots (8-byte stride)", "abi2/probe.c", "call_many", stack_arg_offsets),
    ("mixed int/float GPR assignment",        "abi2/probe.c", "call_mixed", gprs_used),
    ("struct <=8 bytes by value",             "abi2/probe2.c", "d8",  gprs_used),
    ("struct 24 bytes by value",              "abi2/probe2.c", "d24", gprs_used),
    ("long long in one GPR",                  "abi2/probe2.c", "dll", gprs_used),
    ("varargs GPR homing",                    "abi2/probe2.c", "vsum", stack_arg_offsets),
    ("vector argument registers",             "vmx/vec2.c",   "def_mixv", vec_regs),
]

def main():
    if not os.path.exists(CLANG):
        print(f"patched clang not built yet: {CLANG}")
        print("run tools/build-llvm.bat first")
        return 2
    if not os.path.exists(CL):
        print(f"XDK compiler not found: {CL}")
        return 2

    work = tempfile.mkdtemp(prefix="rxdk360-verify-")
    width = max(len(r[0]) for r in RULES)
    agree = differ = skipped = 0
    for label, rel, fn, extract in RULES:
        src = os.path.normpath(os.path.join(SPIKE, rel))
        if not os.path.exists(src):
            print(f"{label:<{width}}  SKIP (missing {rel})"); skipped += 1; continue
        mtext, merr = ms_disasm(src, work)
        ctext, cerr = clang_asm(src, work)
        if mtext is None or ctext is None:
            print(f"{label:<{width}}  SKIP (compile failed)")
            if merr: print("    cl:", merr.strip().splitlines()[-1:])
            if cerr: print("    clang:", cerr.strip().splitlines()[-1:])
            skipped += 1; continue
        a = extract(func(mtext, fn, True))
        b = extract(func(ctext, fn, False))
        ok = a == b
        agree += ok; differ += (not ok)
        print(f"{label:<{width}}  {'OK  ' if ok else 'DIFF'}  ms={a}  clang={b}")
    print(f"\n{agree} agree, {differ} differ, {skipped} skipped")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if differ else 0

sys.exit(main())
