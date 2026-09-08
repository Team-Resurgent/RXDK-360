"""Scan XDK headers for public C prototypes whose arguments land in different
registers under MS-PPC vs LLVM PPC EABI.

Detects the two dominant causes: a float/double parameter followed by an
integer/pointer parameter (MS skips the GPR, EABI does not), and more than
8 register-class parameters (8-byte vs 4-byte stack slots).
"""
import re, sys, os, collections

FLOAT = re.compile(r'\b(float|double|FLOAT|DOUBLE|D3DVALUE)\b')
PROTO = re.compile(
    r'^[ \t]*(?:__declspec\([^)]*\)[ \t]*)?'
    r'(?:extern[ \t]+"C"[ \t]+)?'
    r'(?:[A-Za-z_][A-Za-z0-9_ \t\*]*?)[ \t\*]+'
    r'(?:WINAPI|__stdcall|__cdecl|APIENTRY)?[ \t]*'
    r'([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(([^;{)]*)\)[ \t]*;', re.M)

def split_params(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(' or ch == '<': depth += 1
        elif ch == ')' or ch == '>': depth -= 1
        if ch == ',' and depth == 0: out.append(cur); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur)
    return [p.strip() for p in out if p.strip() and p.strip() != 'void']

def main(paths):
    tot = 0; div = 0; why = collections.Counter(); examples = []
    for path in paths:
        try: src = open(path, encoding='utf-8', errors='replace').read()
        except OSError: continue
        src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
        for m in PROTO.finditer(src):
            name, plist = m.group(1), m.group(2)
            if name in ('if','for','while','switch','sizeof','return'): continue
            ps = split_params(plist)
            if not ps: continue
            tot += 1
            kinds = ['f' if (FLOAT.search(p) and '*' not in p and '&' not in p)
                     else 'i' for p in ps]
            r = []
            if len(ps) > 8: r.append('>8 params')
            if 'f' in kinds:
                fi = kinds.index('f')
                if 'i' in kinds[fi:]: r.append('int-after-float')
            if r:
                div += 1
                for x in r: why[x] += 1
                if len(examples) < 8:
                    examples.append(f"{os.path.basename(path)}: {name}({', '.join(ps)[:70]}...)")
    print(f"public C prototypes scanned: {tot}")
    print(f"  DIVERGENT: {div} ({div*100//max(tot,1)}%)")
    for k, v in why.most_common(): print(f"    {k}: {v}")
    print("  examples:")
    for e in examples: print("   ", e)

main(sys.argv[1:])
