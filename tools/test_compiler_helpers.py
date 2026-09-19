#!/usr/bin/env python3
"""Regression guard for issue #1 (missing compiler-helper runtime).

clang compiling for PowerPC emits calls to small out-of-line helper routines
that are not inline-able: the MS register save/restore family
(__savegprlr_N / __restgprlr_N, __savefpr_N / __restfpr_N, __savevmx_N /
__restvmx_N), the 64-bit-int-to-double conversion __u64tod, and the stack probe
_RtlCheckStack / _RtlCheckStack12. Without them every non-trivial clang title
fails to link with ~30 unresolved symbols. tools/build_libc.py bundles these
into build/libc/libc.a from the translated libcMT; this test fails if any of
them ever drops out of the shipped modern runtime.

This is the symbol-level guard; tests/corpus/regsave exercises the code paths at
runtime. Complements tools/lib_linktest.py, which catches the inverse (a NEW
undefined symbol the runtime owes).

    python tools/test_compiler_helpers.py            # checks build/libc/libc.a
    python tools/test_compiler_helpers.py <lib.a>
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from coff2elf import read_archive  # noqa: E402

DEFAULT_LIB = os.path.join(ROOT, "build", "libc", "libc.a")

# Representative symbols from each helper family (build_libc's MS_GLUE_MEMBERS:
# crtgpr / crtfpr / crtvmx / u64tod / chkstk / jmpuwind). The _14 variants are
# the entry a function saving registers 14..31 calls; if the family is present
# at all, these are.
REQUIRED = [
    "__savegprlr_14", "__restgprlr_14",     # crtgpr.o  (GPR save/restore)
    "__savefpr_14", "__restfpr_14",         # crtfpr.o  (FPR save/restore)
    "__savevmx_14", "__restvmx_14",         # crtvmx.o  (VMX/AltiVec save/restore)
    "__u64tod",                             # u64tod.o  (uint64 -> double)
    "_RtlCheckStack", "_RtlCheckStack12",   # chkstk.o  (stack probe)
    "__jump_unwind",                        # jmpuwind.o (SEH longjmp-unwind)
]


def elf_defined_symbols(blob):
    """Global/weak symbols an ELF object DEFINES (st_shndx != SHN_UNDEF)."""
    if blob[:4] != b"\x7fELF":
        return set()
    e = ">" if blob[5] == 2 else "<"
    e_shoff = struct.unpack_from(e + "I", blob, 0x20)[0]
    e_shentsize, e_shnum, _ = struct.unpack_from(e + "HHH", blob, 0x2E)

    def sh(i):
        return struct.unpack_from(e + "IIIIIIIIII", blob, e_shoff + i * e_shentsize)

    out = set()
    for i in range(e_shnum):
        s = sh(i)
        if s[1] != 2:                       # SHT_SYMTAB
            continue
        off, size, link, ent = s[4], s[5], s[6], s[9]
        stroff = sh(link)[4]
        for k in range(size // ent):
            o = off + k * ent
            st_name, _val, _sz, st_info, _oth, st_shndx = \
                struct.unpack_from(e + "IIIBBH", blob, o)
            bind = st_info >> 4
            if st_name and st_shndx != 0 and bind in (1, 2):   # GLOBAL / WEAK, defined
                end = blob.index(b"\0", stroff + st_name)
                out.add(blob[stroff + st_name:end].decode("latin1"))
    return out


def defined_in_archive(path):
    defined = set()
    for member, _ in read_archive(open(path, "rb").read()):
        defined |= elf_defined_symbols(member.data)
    return defined


def main():
    lib = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LIB
    if not os.path.exists(lib):
        print("FAIL: %s not built" % lib)
        return 1
    have = defined_in_archive(lib)
    missing = [s for s in REQUIRED if s not in have]
    for s in REQUIRED:
        print("  %-18s %s" % (s, "ok" if s in have else "MISSING"))
    if missing:
        print("\nFAIL: %s is missing %d compiler helper(s): %s"
              % (os.path.relpath(lib, ROOT), len(missing), ", ".join(missing)))
        return 1
    print("\nPASS: all %d compiler helpers present in %s"
          % (len(REQUIRED), os.path.relpath(lib, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
