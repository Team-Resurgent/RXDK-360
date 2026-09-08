"""Count public-API constructs that diverge between MS-PPC and LLVM PPC EABI but that
header_scan.py cannot see: struct-by-value parameters, 64-bit integer parameters, and
callback (function-pointer) typedefs whose own signatures diverge.
"""
import re, sys, os, collections

INT64 = re.compile(r'\b(long long|__int64|LONGLONG|ULONGLONG|LARGE_INTEGER|ULARGE_INTEGER|INT64|UINT64|QWORD|LONGLONG)\b')
FLOAT = re.compile(r'\b(float|double|FLOAT|DOUBLE|D3DVALUE)\b')
SCALAR = re.compile(r'\b(void|char|short|int|long|unsigned|signed|float|double|BOOL|BOOLEAN|BYTE|WORD|DWORD|UINT|INT|LONG|ULONG|USHORT|SHORT|CHAR|WCHAR|HANDLE|HRESULT|SIZE_T|FLOAT|DOUBLE|D3DVALUE|D3DCOLOR|LPCSTR|LPSTR|LPCWSTR|LPWSTR|LPVOID|PVOID|va_list|\.\.\.)\b')
PROTO = re.compile(r'^[ \t]*(?:__declspec\([^)]*\)[ \t]*)?(?:extern[ \t]+"C"[ \t]+)?'
                   r'(?:[A-Za-z_][A-Za-z0-9_ \t\*]*?)[ \t\*]+'
                   r'(?:WINAPI|__stdcall|__cdecl|APIENTRY)?[ \t]*'
                   r'([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(([^;{)]*)\)[ \t]*;', re.M)
CBTYPEDEF = re.compile(r'typedef[^;()]*\(\s*(?:WINAPI|__stdcall|__cdecl|APIENTRY)?\s*\*\s*'
                       r'([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\(([^;)]*)\)\s*;')

def params(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '(<': depth += 1
        elif ch in ')>': depth -= 1
        if ch == ',' and depth == 0: out.append(cur); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur)
    return [p.strip() for p in out if p.strip() and p.strip() != 'void']

AGGREGATE = set()   # typedef names that really are struct/union/class
SCALARTD  = set()   # typedef names that resolve to a scalar or an enum

def learn_typedefs(src):
    # typedef struct/union { ... } NAME, *PNAME;   and   typedef struct TAG NAME;
    for m in re.finditer(r'typedef\s+(struct|union|class)[^;{]*\{.*?\}\s*([^;]+);', src, re.S):
        for nm in m.group(2).split(','):
            nm = nm.strip()
            if nm.startswith('*'): SCALARTD.add(nm.lstrip('* ')) # pointer typedef
            elif nm: AGGREGATE.add(nm)
    for m in re.finditer(r'typedef\s+(struct|union|class)\s+\w+\s+([^;]+);', src):
        for nm in m.group(2).split(','):
            nm = nm.strip()
            if nm.startswith('*'): SCALARTD.add(nm.lstrip('* '))
            elif nm: AGGREGATE.add(nm)
    # typedef enum ... NAME;  -> integer-sized
    for m in re.finditer(r'typedef\s+enum[^;{]*(?:\{.*?\})?\s*([^;]+);', src, re.S):
        for nm in m.group(1).split(','): SCALARTD.add(nm.strip().lstrip('* '))
    # typedef <scalar words> NAME;
    for m in re.finditer(r'typedef\s+((?:unsigned|signed|const|long|short|int|char|float|double|void|__int64|wchar_t|\w+)[\w 	]*?)\s*\*?\s*(\w+)\s*;', src):
        SCALARTD.add(m.group(2))

def looks_struct_byvalue(p):
    if '*' in p or '&' in p or '[' in p: return False
    if SCALAR.search(p): return False
    toks = [t for t in re.split(r'[\s]+', p.replace('const','').strip()) if t]
    if not toks: return False
    ty = toks[0]
    if re.match(r'^(struct|union|class)$', ty) : return True
    if ty in AGGREGATE: return True
    return False

def classify(ps):
    r = []
    kinds = ['f' if (FLOAT.search(p) and '*' not in p) else 'i' for p in ps]
    if len(ps) > 8: r.append('>8 params')
    if 'f' in kinds and 'i' in kinds[kinds.index('f'):]: r.append('int-after-float')
    if any(INT64.search(p) and '*' not in p for p in ps): r.append('64-bit int param')
    if any(looks_struct_byvalue(p) for p in ps): r.append('struct-by-value')
    return r

def main(paths):
    for path in paths:      # first pass: learn every typedef before classifying
        try: learn_typedefs(re.sub(r'/\*.*?\*/',' ',open(path,encoding='utf-8',errors='replace').read(),flags=re.S))
        except OSError: pass
    fn_tot = fn_div = cb_tot = cb_div = 0
    fw, cw = collections.Counter(), collections.Counter()
    for path in paths:
        try: src = open(path, encoding='utf-8', errors='replace').read()
        except OSError: continue
        src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
        learn_typedefs(src)
        for m in PROTO.finditer(src):
            if m.group(1) in ('if','for','while','switch','sizeof','return'): continue
            ps = params(m.group(2))
            if not ps: continue
            fn_tot += 1
            r = classify(ps)
            if r:
                fn_div += 1
                for x in r: fw[x] += 1
        for m in CBTYPEDEF.finditer(src):
            ps = params(m.group(2))
            cb_tot += 1
            r = classify(ps)
            if r:
                cb_div += 1
                for x in r: cw[x] += 1
    print(f"public C prototypes : {fn_tot}, divergent {fn_div} ({fn_div*100//max(fn_tot,1)}%)")
    for k,v in fw.most_common(): print(f"    {k}: {v}")
    print(f"callback typedefs   : {cb_tot}, divergent {cb_div} ({cb_div*100//max(cb_tot,1)}%)")
    for k,v in cw.most_common(): print(f"    {k}: {v}")

main(sys.argv[1:])
