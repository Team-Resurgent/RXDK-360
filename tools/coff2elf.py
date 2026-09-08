#!/usr/bin/env python3
"""Translate the XDK's PPC COFF import/static libraries to PPC32 ELF archives.

The hybrid RXDK-360 toolchain reuses Microsoft's shipped `lib\\xbox\\*.lib`
rather than reimplementing them. Those are MS archives of big-endian PowerPC
COFF objects (machine 0x01F2), a format lld cannot read. This rewrites each
object as a PPC32 big-endian ELF and repacks the archive as a System V `.a`
that lld links natively.

Only what the shipped libraries actually use is handled -- the relocation set
was measured at five real types (ADDR32, REL24, REFHI, REFLO, PAIR) plus the
droppable debug-only SECTION/SECREL. Everything else is reported rather than
guessed at, so an unexpected construct surfaces loudly instead of miscompiling.

This module is being built in stages. Right now it parses and reports; ELF
emission follows once the input structure is confirmed against dumpbin on the
real libraries.

Usage:
    python tools/coff2elf.py dump   <lib>            structure of the first object
    python tools/coff2elf.py survey <lib> [lib ...]  tally reloc/section/class use
"""
import argparse
import struct
import sys
from collections import Counter


# ---- Microsoft archive (.lib) ----------------------------------------------

ARCHIVE_MAGIC = b"!<arch>\n"


class Member:
    __slots__ = ("name", "data", "offset")

    def __init__(self, name, data, offset):
        self.name = name
        self.data = data
        self.offset = offset


def read_archive(blob):
    """Yield Member objects for every archive member, in file order.

    The two linker members (both named "/") and the longnames member ("//")
    are yielded too; callers skip them by name. Long member names are resolved
    through the longnames member per the COFF archive spec.
    """
    if blob[:8] != ARCHIVE_MAGIC:
        sys.exit("not a Microsoft archive: bad !<arch> magic")
    pos = 8
    longnames = b""
    members = []
    raw = []
    while pos + 60 <= len(blob):
        header = blob[pos:pos + 60]
        if header[58:60] != b"`\n":
            sys.exit(f"malformed archive header at 0x{pos:X}")
        name = header[0:16].decode("ascii", "replace").rstrip()
        size = int(header[48:58].decode("ascii").strip())
        data_off = pos + 60
        data = blob[data_off:data_off + size]
        raw.append((name, data, data_off))
        pos = data_off + size + (size & 1)          # members are 2-byte aligned

    for i, (name, data, off) in enumerate(raw):
        if i == 2 and name == "//":
            longnames = data
            continue
        yield Member(name, data, off), longnames


def member_name(name, longnames):
    """Resolve a member's real name: '/123' indexes the longnames member."""
    if name.startswith("/") and name[1:].isdigit():
        start = int(name[1:])
        end = longnames.find(b"\0", start)
        return longnames[start:end].decode("ascii", "replace")
    return name.rstrip("/")


# ---- PPC COFF object --------------------------------------------------------

IMAGE_FILE_MACHINE_POWERPCBE = 0x01F2

# IMAGE_REL_PPC_* relocation types, from winnt.h. The numbering was confirmed
# against real objects: the debug sections carry SECREL/SECTION, which show up
# as 0x0B/0x0C, fixing the high types (REFHI/REFLO/PAIR) at 0x10-0x12.
REL_TYPES = {
    0x0000: "ABSOLUTE", 0x0001: "ADDR64", 0x0002: "ADDR32", 0x0003: "ADDR24",
    0x0004: "ADDR16", 0x0005: "ADDR14", 0x0006: "REL24", 0x0007: "REL14",
    0x000A: "ADDR32NB", 0x000B: "SECREL", 0x000C: "SECTION", 0x000F: "SECREL16",
    0x0010: "REFHI", 0x0011: "REFLO", 0x0012: "PAIR", 0x0013: "SECRELLO",
    0x0015: "GPREL", 0x0016: "TOCREL16", 0x0017: "TOCREL14",
}

# The low bits (0x60) of the type word are a "restart" field on some MS PPC
# relocations; mask them off before lookup.
REL_TYPE_MASK = 0x00FF

STORAGE_CLASS = {
    2: "EXTERNAL", 3: "STATIC", 6: "LABEL", 103: "FILE", 105: "SECTION",
    101: "FUNCTION", 100: "BLOCK", 104: "STRTAG", 3: "STATIC",
}


class Section:
    __slots__ = ("name", "vsize", "vaddr", "rawsize", "rawptr", "relptr",
                 "nreloc", "flags", "data", "relocs")


class Symbol:
    __slots__ = ("name", "value", "secnum", "type", "cls", "naux", "aux")


class CoffObject:
    """A parsed big-endian PowerPC COFF object.

    Every scalar in the COFF headers is little-endian regardless of the target
    byte order; only the section *contents* are big-endian PPC. That split is
    the usual source of COFF bugs, so it is called out at each unpack.
    """

    def __init__(self, blob):
        self.blob = blob
        (self.machine, self.nsections, self.timestamp, self.symptr,
         self.nsymbols, self.optsize, self.characteristics) = struct.unpack_from(
            "<HHIIIHH", blob, 0)

        self.sections = []
        off = 20 + self.optsize
        for _ in range(self.nsections):
            s = Section()
            (name, s.vsize, s.vaddr, s.rawsize, s.rawptr, s.relptr,
             _lineptr, s.nreloc, _nline, s.flags) = struct.unpack_from(
                "<8sIIIIIIHHI", blob, off)
            s.name = self._section_name(name)
            s.data = blob[s.rawptr:s.rawptr + s.rawsize] if s.rawptr else b""
            s.relocs = self._read_relocs(s)
            self.sections.append(s)
            off += 40

        self.symbols = self._read_symbols()

    def _strtab(self):
        # the string table follows the symbol table; its first 4 bytes are its
        # own size, and symbol/section name offsets are measured from its start
        base = self.symptr + self.nsymbols * 18
        return base

    def _section_name(self, raw):
        if raw[0:1] == b"/":                       # "/123" -> offset into strtab
            offset = int(raw[1:].split(b"\0")[0].decode("ascii"))
            base = self._strtab()
            end = self.blob.find(b"\0", base + offset)
            return self.blob[base + offset:end].decode("ascii", "replace")
        return raw.split(b"\0")[0].decode("ascii", "replace")

    def _read_relocs(self, s):
        out = []
        for i in range(s.nreloc):
            o = s.relptr + i * 10
            vaddr, symidx, typ = struct.unpack_from("<IIH", self.blob, o)
            out.append((vaddr, symidx, typ))
        return out

    def _sym_name(self, raw):
        if raw[0:4] == b"\0\0\0\0":
            offset, = struct.unpack_from("<I", raw, 4)
            base = self._strtab()
            end = self.blob.find(b"\0", base + offset)
            return self.blob[base + offset:end].decode("ascii", "replace")
        return raw.split(b"\0")[0].decode("ascii", "replace")

    def _read_symbols(self):
        out = []
        i = 0
        while i < self.nsymbols:
            o = self.symptr + i * 18
            raw = self.blob[o:o + 18]
            sym = Symbol()
            sym.name = self._sym_name(raw[0:8])
            sym.value, sym.secnum, sym.type, sym.cls, sym.naux = struct.unpack_from(
                "<iHHBB", raw, 8)
            sym.aux = [self.blob[o + 18 + k * 18:o + 18 + (k + 1) * 18]
                       for k in range(sym.naux)]
            out.append(sym)
            i += 1 + sym.naux                       # aux records share the index space
        return out


# ---- reporting --------------------------------------------------------------

def first_coff(path):
    blob = open(path, "rb").read()
    for member, longnames in read_archive(blob):
        if member.name in ("/", "//"):
            continue
        if member.data[:2] == struct.pack("<H", IMAGE_FILE_MACHINE_POWERPCBE):
            return member_name(member.name, longnames), CoffObject(member.data)
    sys.exit("no PowerPC COFF member found")


def cmd_dump(path):
    name, obj = first_coff(path)
    print(f"{path} :: {name}")
    print(f"  machine 0x{obj.machine:04X}  sections {obj.nsections}  "
          f"symbols {obj.nsymbols}  chars 0x{obj.characteristics:04X}")
    print("  sections:")
    for s in obj.sections:
        rt = Counter(REL_TYPES.get(t, f"0x{t:X}") for _, _, t in s.relocs)
        rt_str = " ".join(f"{k}={v}" for k, v in sorted(rt.items()))
        print(f"    {s.name:<10} size {s.rawsize:>7}  flags 0x{s.flags:08X}  "
              f"relocs {s.nreloc:>4}  {rt_str}")
    print("  first 12 symbols:")
    shown = 0
    for sym in obj.symbols:
        if shown >= 12:
            break
        print(f"    [{sym.secnum:>3}] cls {sym.cls:>3} val 0x{sym.value:08X}  {sym.name}")
        shown += 1


def cmd_survey(paths):
    reltypes = Counter()
    sections = Counter()
    classes = Counter()
    machines = Counter()
    nobjs = 0
    for path in paths:
        blob = open(path, "rb").read()
        for member, longnames in read_archive(blob):
            if member.name in ("/", "//"):
                continue
            if member.data[:2] != struct.pack("<H", IMAGE_FILE_MACHINE_POWERPCBE):
                machines[member.data[:2].hex()] += 1
                continue
            obj = CoffObject(member.data)
            machines[f"{obj.machine:04X}"] += 1
            nobjs += 1
            for s in obj.sections:
                sections[s.name.rstrip("0123456789") or s.name] += 1
                for _, _, t in s.relocs:
                    reltypes[REL_TYPES.get(t, f"UNKNOWN_0x{t:X}")] += 1
            for sym in obj.symbols:
                classes[STORAGE_CLASS.get(sym.cls, f"cls{sym.cls}")] += 1
    print(f"objects: {nobjs}")
    print(f"machines: {dict(machines)}")
    print("relocation types:")
    for k, v in reltypes.most_common():
        print(f"  {k:<16} {v}")
    print("section names (numeric suffix stripped):")
    for k, v in sections.most_common(20):
        print(f"  {k:<16} {v}")
    print("symbol storage classes:")
    for k, v in classes.most_common():
        print(f"  {k:<16} {v}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("dump")
    d.add_argument("lib")
    s = sub.add_parser("survey")
    s.add_argument("libs", nargs="+")
    args = ap.parse_args()

    if args.cmd == "dump":
        cmd_dump(args.lib)
    elif args.cmd == "survey":
        cmd_survey(args.libs)


if __name__ == "__main__":
    main()
