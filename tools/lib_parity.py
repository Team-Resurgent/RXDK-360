#!/usr/bin/env python3
"""Surface-parity audit: which symbols an official XDK lib provides that our
replacement archive does not.

Reads the ARCHIVE SYMBOL INDEX (the first "/" linker member) of any !<arch>
archive -- this lists exactly the externally-visible DEFINED symbols the archive
can satisfy a link with. The first linker member is big-endian in both GNU ar
(our ELF libc.a) and MS COFF (the XDK .lib), so one parser handles both.

    python tools/lib_parity.py <official.lib> <ours.a> [ours2.a ...]

Prints: symbols in <official.lib> not provided by the union of the ours*.a
archives (the gap), plus a summary. C symbols on PPC MS-ABI are undecorated, so
the C-runtime (libcMT) diff is meaningful; the C++ runtime (libcpMT) is
MSVC-mangled vs our Itanium/libc++ and is NOT comparable by raw name.
"""
import struct
import sys


def _members(blob):
    """Yield (name, data) for each archive member."""
    if blob[:8] != b"!<arch>\n":
        raise ValueError("not an !<arch> archive")
    off = 8
    n = len(blob)
    while off + 60 <= n:
        header = blob[off:off + 60]
        name = header[0:16].decode("latin1").rstrip()
        size = int(header[48:58].decode("latin1").strip() or "0")
        data = blob[off + 60:off + 60 + size]
        yield name, data
        off += 60 + size
        if off & 1:            # members are 2-byte aligned
            off += 1


def archive_symbol_index(path):
    """Set of defined external symbols the archive provides."""
    blob = open(path, "rb").read()
    for name, data in _members(blob):
        # first linker member is named "/" (GNU) or "/" (MS 1st linker member)
        if name in ("/", ""):
            if len(data) < 4:
                return set()
            (count,) = struct.unpack_from(">I", data, 0)
            names_off = 4 + 4 * count
            syms, cur, i = set(), names_off, 0
            while i < count and cur < len(data):
                end = data.index(b"\x00", cur)
                syms.add(data[cur:end].decode("latin1"))
                cur = end + 1
                i += 1
            return syms
    return set()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    official = archive_symbol_index(sys.argv[1])
    ours = set()
    for p in sys.argv[2:]:
        ours |= archive_symbol_index(p)

    # Ignore pure import thunks and the archive's own housekeeping entries.
    def real(s):
        return s and not s.startswith("__imp_")

    off = {s for s in official if real(s)}
    our = {s for s in ours if real(s)}
    gap = sorted(off - our)
    extra = sorted(our - off)

    print("official %s: %d provided symbols" % (sys.argv[1].split("\\")[-1], len(off)))
    print("ours (%d archive[s]): %d provided symbols" % (len(sys.argv) - 2, len(our)))
    print("shared: %d   gap (official not in ours): %d   ours-only: %d"
          % (len(off & our), len(gap), len(extra)))
    print("=" * 72)
    print("GAP -- official symbols our replacement does NOT provide:")
    for s in gap:
        print("  " + s)


if __name__ == "__main__":
    main()
