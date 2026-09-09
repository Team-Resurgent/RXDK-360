#!/usr/bin/env python3
"""Generate XEX import thunks for the kernel functions a title calls.

Compiled code that calls, say, DbgPrint emits a `bl DbgPrint` with DbgPrint left
undefined. This reads those undefined symbols from the object, looks each up in
the XDK's import libraries (xboxkrnl.lib etc., whose short-import members carry
the real console ordinals), and emits:

  * a PPC assembly stub the title links against -- one 16-byte thunk named after
    each function (so the `bl` resolves) plus a 4-byte variable record; the
    loader rewrites the thunk to a syscall and resolves the ordinal, and
  * a JSON manifest naming the records per library, for elf2xex --import-manifest.

The thunks go in their own section, away from the entry code, because xenia
declares each thunk as its own function and one abutting the entry's basic block
makes the entry look like an undefined extern.

Usage:
    python tools/gen_import_stubs.py <title.o> --xdk <lib dir> \\
        -o stubs.s --manifest stubs.json
"""
import argparse
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coff2elf import read_archive, parse_short_import, IMAGE_FILE_MACHINE_POWERPCBE


# module (DLL) name the XEX import header should carry, by import-lib basename.
MODULE_NAMES = {
    "xboxkrnl": "xboxkrnl.exe",
    "xbdm": "xbdm.xex",
}


# Kernel exports the console has but the public XDK import libraries do not
# expose (Microsoft did not ship import stubs for them). The ordinals are real
# xboxkrnl.exe ordinals -- verified against xenia's export table -- so importing
# them by ordinal resolves on both xenia and hardware. Used only as a fallback
# when a symbol is missing from the XDK libs.
SUPPLEMENTAL_ORDINALS = {
    "KeTlsAlloc":    ("xboxkrnl.exe", 0x152),
    "KeTlsFree":     ("xboxkrnl.exe", 0x153),
    "KeTlsGetValue": ("xboxkrnl.exe", 0x154),
    "KeTlsSetValue": ("xboxkrnl.exe", 0x155),
}


def build_ordinal_index(xdk_lib_dir):
    """name -> (module_name, ordinal, is_var) across the XDK import libraries.

    The importing module is taken from each short-import's own DLL field (e.g.
    "xam.xex@21256.0+1861.0" -> xam.xex), not the containing .lib -- a single lib
    such as xapilib.lib carries stubs for several modules (xam.xex functions like
    XGetLanguage live inside xapilib.lib, not a xam.lib). is_var is true for a
    data export (a variable, e.g. ExLoadedCommandLine): it needs the variable
    import form, not a call thunk."""
    index = {name: (mod, ordv, False) for name, (mod, ordv) in SUPPLEMENTAL_ORDINALS.items()}
    for path in glob.glob(os.path.join(xdk_lib_dir, "*.lib")):
        base = os.path.splitext(os.path.basename(path))[0].lower()
        blob = open(path, "rb").read()
        for member, _longnames in read_archive(blob):
            if member.name in ("/", "//"):
                continue
            if member.data[:2] == struct.pack("<H", IMAGE_FILE_MACHINE_POWERPCBE):
                continue
            try:
                sym, dll, ordinal, _nt, itype = parse_short_import(member.data)
            except Exception:
                continue
            if not ordinal:
                continue
            module = dll.split("@")[0].strip()      # "xam.xex@..." -> "xam.xex"
            module = module or MODULE_NAMES.get(base)
            if module:
                # IMPORT_OBJECT_DATA(1)/_CONST(2) => a variable, not a function.
                index.setdefault(sym, (module, ordinal, itype in (1, 2)))
    return index


def read_undefined_symbols(blob):
    """Undefined global symbol names from an ELF32 big-endian object."""
    if blob[:4] != b"\x7fELF":
        sys.exit("not an ELF object")
    (e_shoff,) = struct.unpack_from(">I", blob, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", blob, 0x2E)

    def sh(i):
        return struct.unpack_from(">IIIIIIIIII", blob, e_shoff + i * e_shentsize)

    symtab = None
    for i in range(e_shnum):
        if sh(i)[1] == 2:                              # SHT_SYMTAB
            symtab = sh(i)
    if not symtab:
        return []
    strtab = sh(symtab[6])
    off, size, entsize, stroff = symtab[4], symtab[5], symtab[9], strtab[4]
    out = []
    for k in range(size // entsize):
        o = off + k * entsize
        st_name, _v, _sz, st_info, _o, st_shndx = struct.unpack_from(">IIIBBH", blob, o)
        bind = st_info >> 4
        if st_shndx == 0 and st_name and bind != 0:    # SHN_UNDEF, global/weak
            end = blob.index(b"\0", stroff + st_name)
            out.append(blob[stroff + st_name:end].decode("utf-8", "replace"))
    return out


def main():
    import json
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("obj", nargs="?",
                    help="compiled title object (.o) with undefined kernel symbols")
    ap.add_argument("--xdk", required=True, help="XDK lib\\xbox directory")
    ap.add_argument("--names", default=None,
                    help="comma-separated import names instead of reading an object "
                         "(e.g. the undefined symbols a full link reports)")
    ap.add_argument("-o", "--out", default="stubs.s")
    ap.add_argument("--manifest", default=None)
    args = ap.parse_args()

    index = build_ordinal_index(args.xdk)
    if args.names:
        undefined = [n for n in args.names.split(",") if n]
    elif args.obj:
        undefined = read_undefined_symbols(open(args.obj, "rb").read())
    else:
        ap.error("provide an object or --names")

    resolved = {}                          # module -> [(name, ordinal, is_var)]
    unresolved = []
    for name in undefined:
        if name in index:
            module, ordinal, is_var = index[name]
            resolved.setdefault(module, []).append((name, ordinal, is_var))
        else:
            unresolved.append(name)

    # Emit the stub assembly. Two import forms, matching how xenia reads each
    # import record's value (top byte selects the kind, xex_module.cc):
    #
    #   function -> a 16-byte call thunk in .kthunks whose first word is
    #     0x010000<ord> (top byte 1 => xenia rewrites it to a syscall so `bl
    #     <name>` calls the export) PLUS a 4-byte .kvars record __imp_<name> =
    #     <ord> (top byte 0; for a function xenia parks 0xDEADC0DE there).
    #
    #   variable -> ONE 4-byte .kvars record = <ord> (top byte 0 => xenia writes
    #     the export's address into it), with the base symbol <name> aliased onto
    #     that same slot and NO .kthunks thunk -- so reading `<name>` as data
    #     yields the pointer the loader patched in (e.g. char* ExLoadedCommandLine).
    #     A thunk here would make xenia treat the export as a function.
    lines = ["# Generated import thunks -- do not edit.", "    .section .kthunks,\"ax\"", ""]
    for module, funcs in resolved.items():
        for name, ordinal, is_var in funcs:
            if is_var:
                continue
            lines += [f"    .globl {name}", f"{name}:",
                      f"    .long 0x{0x01000000 | ordinal:08X}, 0, 0, 0", ""]
    lines += ["    .section .kvars,\"a\"", ""]
    for module, funcs in resolved.items():
        for name, ordinal, is_var in funcs:
            lines += [f"    .globl __imp_{name}"]
            if is_var:                     # base symbol reads the patched pointer
                lines += [f"    .globl {name}", "    .p2align 2", f"{name}:"]
            lines += [f"__imp_{name}:", f"    .long 0x{ordinal:08X}", ""]
    with open(args.out, "w", newline="\n") as f:
        f.write("\n".join(lines))

    # Manifest: one entry per import record address, exactly once (xenia patches
    # a slot the first time it sees it; a second listing would re-read the patched
    # value as a bogus record). A function has two records (var slot + thunk); a
    # variable has one (the shared slot).
    def records_for(funcs):
        out = []
        for name, _o, is_var in funcs:
            out.append(f"__imp_{name}")
            if not is_var:
                out.append(name)
        return out

    manifest_path = args.manifest or (args.out.rsplit(".", 1)[0] + ".imports.json")
    manifest = {"libraries": [
        {"module": module, "records": records_for(funcs)}
        for module, funcs in resolved.items()]}
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)

    total = sum(len(v) for v in resolved.values())
    nvar = sum(1 for v in resolved.values() for _n, _o, iv in v if iv)
    print(f"{args.out}: {total} imports across {len(resolved)} module(s)"
          + (f" ({nvar} variable)" if nvar else ""))
    for module, funcs in resolved.items():
        print(f"  {module}: " + ", ".join(
            f"{n}(0x{o:X}{'/var' if iv else ''})" for n, o, iv in funcs))
    if unresolved:
        print(f"  unresolved (not kernel imports): {', '.join(unresolved)}")


if __name__ == "__main__":
    main()
