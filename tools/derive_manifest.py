#!/usr/bin/env python3
"""Post-link import step for the global kernel import library.

The modern clang toolset links every title against ONE archive, kernel_import.a
(tools/build_import_lib.py), whose thunks leave the record word's module_index
field zero. --gc-sections keeps only the imports the title actually reaches.
This tool reads those surviving thunks back out, groups them into the XEX import
name table exactly as XexTool's `genstubs` would have, and:

  1. patches the real module_index into each kept record word IN THE ELF, and
  2. writes the same JSON `pack --import-manifest` consumes.

So the build is: link kernel_import.a -> derive_manifest -> `pack
--import-manifest`, with no per-title trial link and no genstubs. `pack` trusts
the pre-baked module_index in the thunks (verified: it does not patch them), so
step 1 is load-bearing -- without it every non-first module resolves against the
wrong library at load time.

    python tools/derive_manifest.py title.elf --xdk <lib\\xbox> -o title.imports.json

The record word layout (matches build_import_lib / gen_import_stubs):
    (kind << 24) | (module_index << 16) | ordinal
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_import_stubs as g          # noqa: E402  build_ordinal_index
import elf2xex                        # noqa: E402  read_elf_symbol_addrs


def _section_offsets(blob):
    """[(addr, off, size)] for sections with real file bytes (not NOBITS)."""
    (e_shoff,) = struct.unpack_from(">I", blob, 0x20)
    e_shentsize, e_shnum = struct.unpack_from(">HH", blob, 0x2E)
    out = []
    for i in range(e_shnum):
        _n, typ, _fl, addr, off, size = struct.unpack_from(
            ">IIIIII", blob, e_shoff + i * e_shentsize)
        if typ != 8 and size:          # 8 = SHT_NOBITS (.bss: no file bytes)
            out.append((addr, off, size))
    return out


def _va_to_off(sections, va):
    for addr, off, size in sections:
        if addr <= va < addr + size:
            return off + (va - addr)
    raise KeyError("VA 0x%08X is in no loadable section" % va)


def derive(blob, xdk_lib_dir):
    """Return (patched_blob, manifest).

    manifest is the genstubs JSON; patched_blob is the ELF with each kept
    record word's module_index field set to its module's name-table index.
    """
    index = g.build_ordinal_index(xdk_lib_dir)
    syms = elf2xex.read_elf_symbol_addrs(blob)          # defined name -> VA
    sections = _section_offsets(blob)

    by_module = {}
    for name, (module, ordinal, is_var) in index.items():
        imp_va = syms.get("__imp_" + name)
        if imp_va is None:
            continue                                    # thunk gc'd; not imported
        thunk_va = None if is_var else syms.get(name)
        by_module.setdefault(module, []).append((name, is_var, thunk_va, imp_va))

    image = bytearray(blob)

    def set_index(va, module_index):
        off = _va_to_off(sections, va)
        (word,) = struct.unpack_from(">I", image, off)
        word = (word & ~0x00FF0000) | (module_index << 16)
        struct.pack_into(">I", image, off, word)

    libraries = []
    # sorted(): the same deterministic module order derive_auto_imports uses, so
    # the name-table index we bake matches the order pack writes the table in.
    for module_index, module in enumerate(sorted(by_module)):
        records = []
        for name, is_var, thunk_va, imp_va in by_module[module]:
            records.append("__imp_" + name)            # the IAT slot (kind 0)
            set_index(imp_va, module_index)
            if not is_var:                             # a function also lists its
                records.append(name)                   # thunk; both words carry it
                set_index(thunk_va, module_index)
                set_index(thunk_va + 4, module_index)
        libraries.append({"module": module, "records": records})
    return bytes(image), {"libraries": libraries}


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("--xdk", default=os.environ.get(
        "RXDK_XDK", r"C:\Program Files (x86)\Microsoft Xbox 360 SDK\lib\xbox"))
    ap.add_argument("-o", "--out", required=True, help="import manifest JSON")
    args = ap.parse_args()

    blob = open(args.elf, "rb").read()
    patched, manifest = derive(blob, args.xdk)
    if patched != blob:
        open(args.elf, "wb").write(patched)             # patch module_index in place
    open(args.out, "w", encoding="utf-8").write(json.dumps(manifest, indent=2))
    n = sum(len(l["records"]) for l in manifest["libraries"])
    print("%s: %d record(s) across %d module(s)"
          % (args.out, n, len(manifest["libraries"])))


if __name__ == "__main__":
    main()
