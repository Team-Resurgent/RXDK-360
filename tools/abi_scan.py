"""Classify MSVC-mangled Xbox 360 exports by whether the MS-PPC and LLVM PPC EABI
calling conventions place their arguments identically.

Divergent when any of:
  * a floating-point parameter is interleaved with integer/pointer parameters
    (MS consumes one positional slot per arg and skips the GPR; EABI keeps
    independent GPR/FPR counters)
  * more than 8 register-class parameters (8-byte vs 4-byte stack slots)
  * a struct/class/union passed by value (MS in registers, EABI by pointer)
  * varargs (MS passes FP varargs in GPRs)
"""
import re, sys, collections

PRIM = {'X':'v','D':'i','C':'i','E':'i','F':'i','G':'i','H':'i','I':'i','J':'i',
        'K':'i','M':'f','N':'f','O':'f','_N':'i','_W':'i','_J':'i','_K':'i',
        '_D':'i','_E':'i','_F':'i','_G':'i','_H':'i','_I':'i','_J':'i'}

class Bail(Exception): pass

def parse_params(s, i, seen):
    """Yield 'i'/'f'/'s' (int-class, float-class, struct-by-value) for each param."""
    out = []
    while i < len(s):
        c = s[i]
        if c == '@':
            return out, i + 1, False
        if c == 'Z':
            # 'Z' here is either the ellipsis (params, then ZZ) or the trailing
            # terminator of a void parameter list (XZ). Only the former is varargs.
            real = [p for p in out if p != 'v']
            va = bool(real) and s[i+1:i+2] == 'Z'
            return out, i + 1, va
        if c.isdigit():                    # back-reference to an earlier type
            idx = int(c)
            out.append(seen[idx] if idx < len(seen) else 'i'); i += 1; continue
        if c in 'PQRS':                    # pointer: P<cv><type>
            i += 1
            if i < len(s) and s[i] in '678':  # function pointer
                raise Bail()
            if i < len(s) and s[i] in 'ABCD': i += 1
            _sub, i, _ = parse_one(s, i, seen)
            out.append('i'); seen.append('i'); continue
        if c in 'AB':                      # reference
            i += 1
            _sub, i, _ = parse_one(s, i, seen)
            out.append('i'); seen.append('i'); continue
        t, i, _ = parse_one(s, i, seen)
        out.append(t); seen.append(t)
    raise Bail()

def parse_one(s, i, seen):
    c = s[i]
    if c == '_':
        k = s[i:i+2]
        if k in PRIM: return PRIM[k], i + 2, False
        raise Bail()
    if c in PRIM: return PRIM[c], i + 1, False
    if c == 'W':                            # enum: W4<name>@@  -> int-sized
        i += 1
        if i < len(s) and s[i].isdigit(): i += 1
        i = skip_name(s, i); return 'i', i, False
    if c in 'UVT':                          # struct/class/union BY VALUE
        i = skip_name(s, i + 1); return 's', i, False
    if c in 'PQRS':
        i += 1
        if i < len(s) and s[i] in 'ABCD': i += 1
        _t, i, _ = parse_one(s, i, seen); return 'i', i, False
    raise Bail()

def skip_name(s, i):
    depth = 0
    while i < len(s):
        if s[i] == '@':
            depth += 1; i += 1
            if depth == 2: return i
        else:
            depth = 0; i += 1
    raise Bail()

def classify(sym):
    m = re.match(r'^\?[^@]*(?:@[^@]+)*@@(?:[A-Z]{2,3})(.*)$', sym)
    if not m: raise Bail()
    body = m.group(1)
    if body.startswith('@'):                # no-arg ctor form  ??0X@@QAA@XZ
        body = body[1:]
    seen = []
    ret, i, _ = parse_one(body, 0, seen)    # return type
    params, _i, va = parse_params(body, i, seen)
    params = [p for p in params if p != 'v']
    reasons = []
    if va: reasons.append('varargs')
    if 's' in params: reasons.append('struct-by-value')
    if len(params) > 8: reasons.append('>8 params')
    if 'f' in params and 'i' in params:
        # interleaved only matters if an int follows a float positionally
        fi = params.index('f')
        if any(p == 'i' for p in params[fi:]): reasons.append('int-after-float')
    return reasons

def main(path):
    tot = safe = div = unparsed = data = 0
    why = collections.Counter()
    for line in open(path):
        sym = line.strip()
        if not sym.startswith('?'): continue
        # vftables / vbtables / thunks / RTTI descriptors and static data carry no
        # calling convention at all - they are not part of the ABI question.
        if sym.startswith('??_') or re.search(r'@@[0-9]', sym): 
            data += 1; continue
        tot += 1
        try:
            r = classify(sym)
        except (Bail, IndexError, RecursionError):
            unparsed += 1; continue
        if r:
            div += 1
            for x in r: why[x] += 1
        else:
            safe += 1
    print(f"{path}: {tot} mangled FUNCTIONS ({data} data/vftable symbols excluded)")
    print(f"  ABI-identical : {safe} ({safe*100//max(tot,1)}%)")
    print(f"  DIVERGENT     : {div} ({div*100//max(tot,1)}%)")
    print(f"  unparsed      : {unparsed}")
    for k, v in why.most_common(): print(f"    {k}: {v}")

for p in sys.argv[1:]: main(p)
