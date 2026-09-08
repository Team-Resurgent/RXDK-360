#!/usr/bin/env python3
"""Pack a linked PPC32 ELF executable into an Xbox 360 XEX2.

The last stage of the toolchain: once code is compiled to ELF and linked at a
XEX load address, this wraps the loadable image in the XEX2 container the 360
loader (and xenia) expects. The first cut emits an uncompressed, unencrypted
devkit XEX -- the form a debug kit will load without a signature -- and is
verified by reading it back with XexTool.

XEX is big-endian throughout.

Usage:
    python tools/elf2xex.py <input.elf> -o <output.xex> [--base 0x82000000]
"""
import argparse
import struct
import sys


PAGE = 0x1000

# optional-header keys (XexTool's XexImageEntryTypes.h)
KEY_BASEFILE_FORMAT = 0x000003FF
KEY_ENTRY_POINT = 0x00010100
KEY_IMAGE_BASE_ADDRESS = 0x00010201
KEY_IMPORT_LIBRARIES = 0x000103FF
KEY_ORIGINAL_BASE_ADDRESS = 0x00010001

# module / image flags
MODULEFLAG_TITLE_MODULE = 0x00000001
MODULEFLAG_USER_MODE = 0x00000080

# section info types
SECTIONINFO_CODE = 1
SECTIONINFO_DATA = 2
SECTIONINFO_READONLY = 3


# ---- ELF32 big-endian reading -----------------------------------------------

# ELF section flags / types
SHF_ALLOC, SHF_WRITE, SHF_EXECINSTR = 0x2, 0x1, 0x4
SHT_NOBITS = 8


class ElfSection:
    __slots__ = ("name", "vaddr", "data", "memsize", "flags", "typ")


def read_elf_sections(blob):
    """Return (load_base, [ElfSection], entry) for the allocatable sections.

    The basefile is built from sections rather than PT_LOAD segments so the
    ELF's own headers do not end up in the image, and so each section can carry
    its address and flags into a matching PE section.
    """
    if blob[:4] != b"\x7fELF":
        sys.exit("not an ELF file")
    if blob[4] != 1 or blob[5] != 2:
        sys.exit("expected a 32-bit big-endian ELF (PPC)")
    (e_entry,) = struct.unpack_from(">I", blob, 0x18)
    (e_phoff,) = struct.unpack_from(">I", blob, 0x1C)
    (e_shoff,) = struct.unpack_from(">I", blob, 0x20)
    e_phentsize, e_phnum = struct.unpack_from(">HH", blob, 0x2A)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", blob, 0x2E)

    # the image base is the lowest PT_LOAD vaddr (what --image-base set), not the
    # lowest section -- lld can place the first section a little above the base
    load_vaddrs = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, _off, p_vaddr = struct.unpack_from(">III", blob, o)
        if p_type == 1:
            load_vaddrs.append(p_vaddr)
    image_base = min(load_vaddrs) if load_vaddrs else None

    def shdr(i):
        o = e_shoff + i * e_shentsize
        return struct.unpack_from(">IIIIIIIIII", blob, o)   # name,type,flags,addr,off,size,...

    strtab_off = shdr(e_shstrndx)[4]

    def name(off):
        end = blob.index(b"\0", strtab_off + off)
        return blob[strtab_off + off:end].decode("utf-8", "replace")

    out = []
    for i in range(e_shnum):
        nm, typ, flags, addr, off, size, *_ = shdr(i)
        if not (flags & SHF_ALLOC) or size == 0:
            continue
        s = ElfSection()
        s.name = name(nm)
        s.vaddr = addr
        s.typ = typ
        s.flags = flags
        s.memsize = size
        s.data = b"" if typ == SHT_NOBITS else blob[off:off + size]
        out.append(s)
    if not out:
        sys.exit("no allocatable sections")
    out.sort(key=lambda s: s.vaddr)
    base = image_base if image_base is not None else out[0].vaddr
    return base, out, e_entry


# ---- little-endian PPC PE32 synthesis ---------------------------------------

# Xbox 360 PE headers are little-endian (verified on a real basefile) even
# though the code they describe is big-endian.
PE_MACHINE_POWERPCBE = 0x01F2
PE_FILE_EXECUTABLE_IMAGE = 0x0002
PE_FILE_32BIT_MACHINE = 0x0100
PE_OPTIONAL_MAGIC_PE32 = 0x010B
PE_SUBSYSTEM_XBOX = 14
PE_SIZEOF_OPTIONAL_HEADER = 224

PE_SCN_CODE = 0x00000020
PE_SCN_INIT_DATA = 0x00000040
PE_SCN_UNINIT_DATA = 0x00000080
PE_SCN_MEM_EXECUTE = 0x20000000
PE_SCN_MEM_READ = 0x40000000
PE_SCN_MEM_WRITE = 0x80000000

SECTION_ALIGN = 0x10000          # XEX images use 64KB pages
FILE_ALIGN = 0x1000


def _pe_section_flags(s):
    f = PE_SCN_MEM_READ
    if s.flags & SHF_EXECINSTR:
        f |= PE_SCN_CODE | PE_SCN_MEM_EXECUTE
    elif s.typ == SHT_NOBITS:
        f |= PE_SCN_UNINIT_DATA
    else:
        f |= PE_SCN_INIT_DATA
    if s.flags & SHF_WRITE:
        f |= PE_SCN_MEM_WRITE
    return f


def build_pe_basefile(base, sections, entry):
    """Assemble the loadable image as a little-endian PPC PE32.

    xenia maps the basefile contiguously at the load address and then reads the
    PE headers from offset 0, so file offset equals RVA: the PE headers sit at
    RVA 0 and each section's raw data sits at its own RVA.
    """
    e_lfanew = 0x40
    nsec = len(sections)
    headers_end = e_lfanew + 4 + 20 + PE_SIZEOF_OPTIONAL_HEADER + nsec * 40
    size_of_headers = (headers_end + FILE_ALIGN - 1) & ~(FILE_ALIGN - 1)

    image_end = base
    for s in sections:
        image_end = max(image_end, s.vaddr + s.memsize)
    size_of_image = ((image_end - base) + SECTION_ALIGN - 1) & ~(SECTION_ALIGN - 1)

    code_rva = next((s.vaddr - base for s in sections if s.flags & SHF_EXECINSTR),
                    size_of_headers)
    size_of_code = sum(len(s.data) for s in sections if s.flags & SHF_EXECINSTR)
    size_of_data = sum(len(s.data) for s in sections if not (s.flags & SHF_EXECINSTR))

    # --- DOS header (just the MZ magic and e_lfanew) ---
    dos = bytearray(e_lfanew)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, e_lfanew)

    # --- file header ---
    file_hdr = struct.pack("<IHHIIIHH",
                           0x00004550,                 # "PE\0\0"
                           PE_MACHINE_POWERPCBE,
                           nsec, 0, 0, 0,
                           PE_SIZEOF_OPTIONAL_HEADER,
                           PE_FILE_EXECUTABLE_IMAGE | PE_FILE_32BIT_MACHINE)

    # --- optional header (PE32, 224 bytes incl. 16 data directories) ---
    opt = struct.pack("<HBBIIIIII",
                      PE_OPTIONAL_MAGIC_PE32, 0, 0,
                      size_of_code, size_of_data, 0,
                      entry - base, code_rva, 0)        # BaseOfCode, BaseOfData
    opt += struct.pack("<IIIHHHHHHIIIIHHIIIIII",
                       base,                            # ImageBase
                       SECTION_ALIGN, FILE_ALIGN,
                       4, 0, 0, 0, 0, 0,                # os/image/subsystem versions
                       0,                               # Win32VersionValue
                       size_of_image, size_of_headers,
                       0,                               # CheckSum
                       PE_SUBSYSTEM_XBOX, 0,            # Subsystem, DllCharacteristics
                       0x40000, 0x1000, 0x100000, 0x1000,  # stack/heap reserve/commit
                       0, 16)                           # LoaderFlags, NumberOfRvaAndSizes
    opt += b"\0" * (16 * 8)                             # data directories
    assert len(opt) == PE_SIZEOF_OPTIONAL_HEADER, len(opt)

    # --- section table ---
    sec_hdrs = b""
    for s in sections:
        nm = s.name.encode("ascii")[:8].ljust(8, b"\0")
        rva = s.vaddr - base
        raw_size = (len(s.data) + FILE_ALIGN - 1) & ~(FILE_ALIGN - 1)
        sec_hdrs += struct.pack("<8sIIIIIIHHI",
                                nm, s.memsize, rva,
                                raw_size, rva,          # SizeOfRawData, PointerToRawData=RVA
                                0, 0, 0, 0,
                                _pe_section_flags(s))

    # --- assemble the image: headers at 0, each section at its RVA ---
    image = bytearray(size_of_image)
    image[0:len(dos)] = dos
    o = e_lfanew
    image[o:o + len(file_hdr)] = file_hdr; o += len(file_hdr)
    image[o:o + len(opt)] = opt; o += len(opt)
    image[o:o + len(sec_hdrs)] = sec_hdrs
    for s in sections:
        rva = s.vaddr - base
        image[rva:rva + len(s.data)] = s.data
    return base, bytes(image), entry, size_of_image


# ---- XEX2 structures --------------------------------------------------------

def build_basefile_format(image_size, zero_size):
    """RawBaseFileInfo: uncompressed (compType=1), unencrypted (encType=0)."""
    info = struct.pack(">iHH", 8 + 8, 0, 1)            # infoSize, encType, compType
    info += struct.pack(">ii", image_size, zero_size)  # one RawBaseFileBlock
    return info


def build_security_info(image_size, load_address, sections):
    """XexSecurityInfo + section table. Hashes are left zero (a dev kit with an
    unsigned image does not check them; they can be filled in later)."""
    ZHASH = b"\0" * 20
    image_info = b""
    image_info += b"\0" * 256                          # signature
    image_info += struct.pack(">i", 0x174)             # infoSize
    image_info += struct.pack(">I", 0)                 # imageFlags (none: unencrypted)
    image_info += struct.pack(">I", load_address)      # loadAddress
    image_info += ZHASH                                # imageHash
    image_info += struct.pack(">i", 0)                 # importTableCount
    image_info += ZHASH                                # importHash
    image_info += b"\0" * 16                           # mediaId
    image_info += b"\0" * 16                           # imageKey (zero: unencrypted)
    image_info += struct.pack(">I", 0)                 # exportTableAddress
    image_info += ZHASH                                # headerHash
    image_info += struct.pack(">I", 0xFFFFFFFF)        # gameRegion (all)
    assert len(image_info) == 0x174, hex(len(image_info))

    sec_bytes = b""
    for pages, info_type in sections:
        sec_bytes += struct.pack(">I", (pages << 4) | (info_type & 0xF)) + ZHASH

    total = 0x184 + len(sec_bytes)
    out = struct.pack(">ii", total, image_size)
    out += image_info
    out += struct.pack(">I", 0xFFFFFFFF)               # allowedMediaTypes (all)
    out += struct.pack(">i", len(sections))            # sectionCount
    out += sec_bytes
    return out


def pack(elf_path, out_path, base_override=None):
    blob = open(elf_path, "rb").read()
    load_base, sections, entry = read_elf_sections(blob)
    if base_override is not None and base_override != load_base:
        sys.exit(f"ELF is linked at 0x{load_base:08X}, not 0x{base_override:08X}; "
                 f"link it at the target base instead of overriding here")

    load_base, image, entry, image_size = build_pe_basefile(load_base, sections, entry)

    # the basefile is the PE image; nothing is zero-trimmed for the first cut.
    # XEX images are paged in 64KB units, so the security-info section table
    # counts 64KB pages (image_size is already 64KB-aligned by the PE builder).
    zero_size = 0
    pages = image_size // 0x10000

    basefile_format = build_basefile_format(len(image), zero_size)
    # one section spanning the whole image, read/write data
    security = build_security_info(image_size, load_base,
                                   [(pages, SECTIONINFO_DATA)])

    # optional-header directory: inline-value keys (low byte 0x00/0x01) carry
    # their value directly; others carry a file offset to their data.
    inline = [
        (KEY_ENTRY_POINT, entry),
        (KEY_IMAGE_BASE_ADDRESS, load_base),
        (KEY_ORIGINAL_BASE_ADDRESS, load_base),
    ]
    offset_entries = [KEY_BASEFILE_FORMAT]            # data appended after headers

    n_entries = len(inline) + len(offset_entries)
    header_size = 0x18 + n_entries * 8

    # layout: image header, directory, then security info, then the
    # offset-entry data blocks, all before the basefile.
    sec_off = header_size
    fmt_off = sec_off + len(security)
    headers_end = fmt_off + len(basefile_format)
    # the basefile starts page-aligned after all headers
    basefile_off = (headers_end + PAGE - 1) & ~(PAGE - 1)

    directory = b""
    for key, val in inline:
        directory += struct.pack(">II", key, val)
    directory += struct.pack(">II", KEY_BASEFILE_FORMAT, fmt_off)

    image_header = struct.pack(">4sIiiiI", b"XEX2",
                               MODULEFLAG_TITLE_MODULE | MODULEFLAG_USER_MODE,
                               basefile_off,          # sizeOfHeaders
                               0,                     # sizeOfDiscardableHeaders (loader checks 0)
                               sec_off,               # securityInfoOffset
                               n_entries)

    out = bytearray()
    out += image_header
    out += directory
    assert len(out) == sec_off
    out += security
    assert len(out) == fmt_off
    out += basefile_format
    out += b"\0" * (basefile_off - len(out))
    out += image

    with open(out_path, "wb") as f:
        f.write(out)
    print(f"wrote {out_path}: base 0x{load_base:08X} entry 0x{entry:08X} "
          f"image {len(image)} bytes ({pages} pages), file {len(out)} bytes")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("-o", "--out", default="out.xex")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=None,
                    help="assert the ELF's load base (does not relocate)")
    args = ap.parse_args()
    pack(args.elf, args.out, args.base)


if __name__ == "__main__":
    main()
