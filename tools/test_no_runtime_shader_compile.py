#!/usr/bin/env python3
"""Regression guard for issue #5 (runtime shader compile wedges the console).

Calling D3DXCompileShader on hardware -- any profile, even a trivial shader --
never returns: XBDM dies, hard reboot required. It is a real, unresolved defect
in the runtime HLSL compiler (not invalid input: fxc compiles the same shaders
fine). The supported practice, and what shipping 360 titles do anyway, is to
precompile shaders offline with fxc (see tests/gfx/build_tri.py) and load the
microcode -- never compile at runtime.

This guard keeps that practice enforced: it fails if any title/test/sample source
reintroduces a runtime shader-compile entry point, so the known-unsafe path can't
creep back in unnoticed. Documented in docs/runtime.md.

    python tools/test_no_runtime_shader_compile.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Runtime HLSL-compile entry points that hang on hardware. Assembling/loading
# precompiled microcode is fine; only the runtime *compiler* is unsafe.
BANNED = [
    "D3DXCompileShader",
    "D3DXCompileShaderFromFile",
    "D3DXCompileShaderFromResource",
    "D3DXAssembleShader",            # same runtime-assembler family
]
BANNED_RE = re.compile("|".join(re.escape(b) for b in BANNED))
COMMENT_RE = re.compile(r"//.*|/\*.*?\*/", re.S)

# Title-facing source trees; the runtime itself doesn't reference these, but a
# test or sample might. Scan C/C++ sources.
SCAN_DIRS = ["tests", "spike"]
EXTS = (".c", ".cpp", ".cc", ".cxx", ".h", ".hpp")


def offending_lines(path):
    text = open(path, "r", encoding="utf-8", errors="replace").read()
    stripped = COMMENT_RE.sub(lambda m: " " * len(m.group(0)), text)  # ignore mentions in comments
    hits = []
    for i, line in enumerate(stripped.splitlines(), 1):
        if BANNED_RE.search(line):
            hits.append(i)
    return hits


def main():
    bad = []
    for d in SCAN_DIRS:
        base = os.path.join(ROOT, d)
        for root, _, files in os.walk(base):
            for fn in files:
                if fn.endswith(EXTS):
                    p = os.path.join(root, fn)
                    for ln in offending_lines(p):
                        bad.append((os.path.relpath(p, ROOT), ln))
    if bad:
        print("FAIL: runtime shader-compile call(s) found (issue #5 -- hangs on HW; "
              "precompile with fxc instead):")
        for path, ln in bad:
            print("  %s:%d" % (path, ln))
        return 1
    print("PASS: no runtime shader-compile entry points in title/test/sample source")
    return 0


if __name__ == "__main__":
    sys.exit(main())
