#!/usr/bin/env python3
"""Per-title link+run test for the shipped XDK middleware libraries.

Unlike lib_linktest.py (which force-links every member of a lib, a worst case),
this builds a small *title* that references a representative entry point of each
target lib and links it the way a real title does -- pulling only the members it
uses, plus the sibling libs and our runtime. Where the lib has a device-free API
it can be called, the title is also RUN in xenia so its static initialisers, the
call, and teardown execute -- the phase that surfaced vcomp's process-heap and
`??_E` teardown bugs, which a link-only test cannot see.

Each entry marks progress with DbgPrint '[LT] ...' lines; a run PASSES when it
reaches '[LT] DONE <lib>' with no RXDK-FAULT.

    python tools/lib_titletest.py [--only a,b] [--keep]
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, "build", "titletest")
LIBCPP = os.path.join(ROOT, "build", "libc", "libcpp.a")

XENIA = os.environ.get(
    "RXDK_XENIA", r"D:\Git\xenia-canary\build\bin\Windows\Release\xenia_canary.exe")
XENIA_CWD = os.environ.get("RXDK_XENIA_CWD", r"D:\Git\xenia_canary_windows")
XENIA_LOG = os.environ.get(
    "RXDK_XENIA_LOG", r"D:\Git\xenia-canary\build\bin\Windows\Release\xenia.log")
LT_RE = re.compile(r"\[LT\]\s+(SECT|PASS|FAIL|DONE)\s*(.*)")
FAULT_RE = re.compile(r"RXDK-FAULT")

# Optional extra libs a given test needs beyond the target lib + runtime (the
# target's own cross-library dependencies). Pre-linking *every* sibling is wrong:
# several MS libs each bundle their own copy of common win32 shims, which then
# collide as duplicate symbols. So link only the target lib plus the deps it
# actually needs, declared per test.
DEPS = {
    "xgraphics": ["d3d9"],     # D3D:: texture-layout helpers live in d3d9
    "xaudio2": ["xmcore"],     # XLFQueue* live in xmcore
}

PRINT = 'extern int DbgPrint(const char*,...);\n'

# name -> (source, runnable). Link-only entries take the address of an exported
# symbol (without calling it) so the lib links and resolves, without needing the
# device/GPU/audio hardware the call itself would.
TESTS = {
    # ---- runnable: device-free compute -------------------------------------
    "d3dx9": (PRINT + r'''
extern float* D3DXMatrixMultiply(float*o,const float*a,const float*b);
static float I[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
int main(void){
    DbgPrint("[LT] SECT d3dx9\n");
    float m[16], s[16];
    for(int i=0;i<16;i++) s[i]=I[i]*2.0f;
    D3DXMatrixMultiply(m,I,s);          /* I * (2I) = 2I */
    DbgPrint("[LT] %s m00=%d\n", (m[0]==2.0f&&m[5]==2.0f)?"PASS":"FAIL", (int)m[0]);
    DbgPrint("[LT] DONE d3dx9\n"); return 0;
}''', True),

    "x3daudio": (PRINT + r'''
extern int X3DAudioInitialize(unsigned mask,float speed,unsigned char*inst);
int main(void){
    DbgPrint("[LT] SECT x3daudio\n");
    unsigned char h[64];                /* X3DAUDIO_HANDLE (20B); over-size */
    int hr = X3DAudioInitialize(3 /*STEREO*/, 343.5f, h);
    DbgPrint("[LT] %s hr=%d\n", (hr>=0)?"PASS":"FAIL", hr);
    DbgPrint("[LT] DONE x3daudio\n"); return 0;
}''', True),

    # ---- link-only: needs GPU/audio/net device to actually run -------------
    "xgraphics": (PRINT + r'''
extern void XGGetTextureLayout(void);
void*volatile s;
int main(void){ DbgPrint("[LT] SECT xgraphics\n"); s=(void*)&XGGetTextureLayout;
    DbgPrint("[LT] DONE xgraphics\n"); return 0; }''', False),

    "d3d9": (PRINT + r'''
extern void Direct3D_CreateDevice(void);
void*volatile s;
int main(void){ DbgPrint("[LT] SECT d3d9\n"); s=(void*)&Direct3D_CreateDevice;
    DbgPrint("[LT] DONE d3d9\n"); return 0; }''', False),

    "xaudio2": (PRINT + r'''
extern void XAudio2Create(void);
void*volatile s;
int main(void){ DbgPrint("[LT] SECT xaudio2\n"); s=(void*)&XAudio2Create;
    DbgPrint("[LT] DONE xaudio2\n"); return 0; }''', False),

    "xnet": (PRINT + r'''
extern void XNetStartup(void);
void*volatile s;
int main(void){ DbgPrint("[LT] SECT xnet\n"); s=(void*)&XNetStartup;
    DbgPrint("[LT] DONE xnet\n"); return 0; }''', False),
}


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def build(name, src, runnable):
    os.makedirs(BUILD, exist_ok=True)
    c = os.path.join(BUILD, name + ".c")
    open(c, "w", newline="\n").write(src)
    out = os.path.join(BUILD, name + ".xex")
    libspec = ",".join([name] + DEPS.get(name, []) + [LIBCPP])
    r = sh([sys.executable, os.path.join(HERE, "mktitle.py"), c,
            "--lib", libspec, "-o", out])
    if r.returncode != 0 or not os.path.exists(out):
        msg = (r.stderr or r.stdout)
        # summarise: the most useful line is the first real error.
        for pat in (r"unresolved symbols \(not kernel imports\): .*",
                    r"ld\.lld: error: undefined symbol: \S+",
                    r"ld\.lld: error: duplicate symbol: \S+",
                    r"ld\.lld: error: .*"):
            m = re.search(pat, msg)
            if m:
                return None, m.group(0).replace("ld.lld: error: ", "")
        last = [ln for ln in msg.strip().splitlines() if ln.strip()]
        return None, (last[-1] if last else "link failed")
    return out, None


def run(xex, name):
    # xenia's async logger can drop the final line on a fast exit, so a run that
    # did not reach DONE (and did not fault) is retried, like run_stdlib_tests.
    for _ in range(4):
        try:
            os.remove(XENIA_LOG)
        except OSError:
            pass
        subprocess.run(["taskkill", "/F", "/IM", "xenia_canary.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            subprocess.run([XENIA, xex, "--headless=true"], cwd=XENIA_CWD,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=40)
        except subprocess.TimeoutExpired:
            pass
        subprocess.run(["taskkill", "/F", "/IM", "xenia_canary.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        done, fault, events = False, False, []
        try:
            for ln in open(XENIA_LOG, "r", encoding="utf-8", errors="replace"):
                m = LT_RE.search(ln)
                if m:
                    events.append((m.group(1), m.group(2).strip()))
                    if m.group(1) == "DONE":
                        done = True
                if FAULT_RE.search(ln):
                    fault = True
        except OSError:
            pass
        if done or fault:
            return done, fault, events
    return done, fault, events


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", help="comma list of lib names")
    args = ap.parse_args()
    names = list(TESTS)
    if args.only:
        want = set(args.only.split(","))
        names = [n for n in names if n in want]

    print("%-12s %-6s %-8s %s" % ("lib", "link", "run", "detail"))
    print("-" * 60)
    for n in names:
        src, runnable = TESTS[n]
        xex, err = build(n, src, runnable)
        if not xex:
            print("%-12s %-6s %-8s %s" % (n, "GAP", "-", err))
            continue
        if not runnable:
            print("%-12s %-6s %-8s %s" % (n, "ok", "skip", "(needs device)"))
            continue
        done, fault, events = run(xex, n)
        checks = [e for e in events if e[0] in ("PASS", "FAIL")]
        passes = sum(1 for k, _ in checks if k == "PASS")
        if fault:
            verdict = "FAULT"
        elif done and all(k == "PASS" for k, _ in checks):
            verdict = "PASS(%d)" % passes
        else:
            verdict = "INCOMPLETE"
        print("%-12s %-6s %-8s %s" % (n, "ok", verdict,
              " ".join("%s:%s" % e for e in checks)))


if __name__ == "__main__":
    main()
