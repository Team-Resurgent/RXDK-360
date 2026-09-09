#!/usr/bin/env python3
"""Compare a title built with the official Xbox 360 XDK toolchain against the
same source built with the RXDK-360 toolchain.

The shared body (tests/official/sample_body.cpp by default) defines
`extern "C" void rxdk_body(void)` plus any file-scope static constructors. This
harness compiles it BOTH ways -- the official cl.exe -> link.exe -> imagexex.exe,
and our clang -> lld -> packer via mktitle.py -- runs each XEX in xenia, and
diffs the guest DbgPrint output with the startup wrapper's trailer removed
(official prints "[XAPI RETURN VALUE] N"; ours prints the corpus sentinel). A
match means our startup, C++ ABI and xapi/kernel imports behave like the
console's own toolchain for that program.

    python tools/compare_official.py [body.cpp]
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
XDK = r"C:\Program Files (x86)\Microsoft Xbox 360 SDK"
XENIA = os.environ.get("RXDK_XENIA",
    r"D:\Git\xenia-canary\build\bin\Windows\Release\xenia_canary.exe")
OUT = os.path.join(ROOT, "build", "official")

TRAILER = re.compile(r"\[XAPI RETURN VALUE\]|##RXDK-CORPUS-END##")


def sh(cmd, env=None):
    return subprocess.run(cmd, capture_output=True, text=True, env=env)


def build_official(body):
    bin32 = os.path.join(XDK, "bin", "win32")
    cl, link, imagexex = (os.path.join(bin32, e)
                          for e in ("cl.exe", "link.exe", "imagexex.exe"))
    env = dict(os.environ)
    env["PATH"] = bin32 + os.pathsep + env["PATH"]
    env["INCLUDE"] = os.path.join(XDK, "include", "xbox")
    env["LIB"] = os.path.join(XDK, "lib", "xbox")
    main = os.path.join(OUT, "off_main.cpp")
    open(main, "w").write('#include <xtl.h>\nextern "C" void rxdk_body(void);\n'
                          'void main(void){ rxdk_body(); }\n')
    obj_b = os.path.join(OUT, "off_body.obj")
    obj_m = os.path.join(OUT, "off_main.obj")
    exe = os.path.join(OUT, "official.exe")
    xex = os.path.join(OUT, "official.xex")
    for src, obj in ((body, obj_b), (main, obj_m)):
        r = sh([cl, "/nologo", "/c", "/MT", "/D_XBOX", "/DXBOX",
                "/Fo" + obj, src], env)
        if r.returncode != 0:
            return None, "cl: " + (r.stdout + r.stderr)
    r = sh([link, "/nologo", "/OUT:" + exe, obj_b, obj_m,
            "xapilib.lib", "xboxkrnl.lib", "libcMT.lib"], env)
    if r.returncode != 0:
        return None, "link: " + (r.stdout + r.stderr)
    r = sh([imagexex, "/nologo", "/IN:" + exe, "/OUT:" + xex], env)
    if r.returncode != 0:
        return None, "imagexex: " + (r.stdout + r.stderr)
    return xex, None


def build_ours(body):
    main = os.path.join(OUT, "our_main.cpp")
    open(main, "w").write('extern "C" void rxdk_body(void);\n'
                          'extern "C" void title_main(void){ rxdk_body(); }\n')
    xex = os.path.join(OUT, "ours.xex")
    r = sh([sys.executable, os.path.join(HERE, "mktitle.py"),
            body, main, os.path.join(ROOT, "tests", "corpus", "_prelude", "start.c"),
            "-o", xex, "--cc", "clang",
            "--lib", os.path.join(ROOT, "build", "libc", "libcpp.a"),
            "--lib", os.path.join(ROOT, "build", "libc", "libc.a"),
            "--lib", os.path.join(ROOT, "build", "coff", "xapilib.a")])
    if r.returncode != 0 or not os.path.exists(xex):
        return None, "mktitle: " + (r.stdout + r.stderr)
    return xex, None


def run(xex):
    log = xex + ".log"
    if os.path.exists(log):
        os.remove(log)
    try:
        subprocess.run([XENIA, xex, "--headless=true", "--log_file=" + log],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=25)
    except subprocess.TimeoutExpired:
        pass
    finally:
        subprocess.run(["taskkill", "/F", "/IM", "xenia_canary.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    lines = []
    for ln in open(log, errors="replace") if os.path.exists(log) else []:
        m = re.search(r"\(DbgPrint\) (.*)", ln)
        if m and not TRAILER.search(m.group(1)):
            lines.append(m.group(1).rstrip())
    return lines


def main():
    body = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(ROOT, "tests", "official", "sample_body.cpp")
    os.makedirs(OUT, exist_ok=True)

    off_xex, err = build_official(body)
    if err:
        sys.exit("official build failed:\n" + err)
    our_xex, err = build_ours(body)
    if err:
        sys.exit("our build failed:\n" + err)

    off = run(off_xex)
    our = run(our_xex)

    print("=== official ===");  [print("  " + l) for l in off]
    print("=== ours ===");      [print("  " + l) for l in our]
    if off == our:
        print("\nMATCH: %d user-output lines identical." % len(off))
        return 0
    print("\nDIFF:")
    for i in range(max(len(off), len(our))):
        a = off[i] if i < len(off) else "<none>"
        b = our[i] if i < len(our) else "<none>"
        if a != b:
            print("  official: %s\n  ours:     %s" % (a, b))
    return 1


if __name__ == "__main__":
    sys.exit(main())
