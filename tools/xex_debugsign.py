#!/usr/bin/env python3
"""Debug-sign an Xbox 360 XEX2 in place -- the self-contained equivalent of
`XexTool -m d`, so the CLI toolchain (elf2xex/mktitle) produces a XEX a real
devkit will load.

Why this exists
---------------
elf2xex packs a valid image but leaves the security-info hashes and the RSA
signature zeroed. A zero signature is classified *retail*, and a real kit then
rejects the title with LDRX C000007B; a wrong hash chain gives C0000221. The VS
build path avoids this by calling XexTool `-m d`. To keep the command-line path
XexTool-free (its whole point), this reimplements the debug-sign natively.

The algorithm was reverse-engineered from Team-Resurgent/XexTool
(src/XexWriter.cpp: updateSectionHashes/updateImportHash/updateHeaderHash/
updateSign) and team-Resurgent/XeCrypt (xecrypt.c, xecryptBn.c), and every step
here is validated byte-for-byte against XexTool's own output (tools that ship the
signed reference; see tests). The four stages, in order (each feeds the next):

  1. section hashes  -> imageInfo.imageHash + each page descriptor's hash
       backward chain: h=0; for each section from last to first,
       store h into the descriptor, then h = SHA1(page_bytes || descriptor[24B]).
  2. import hashes   -> imageInfo.importHash + each import lib's digest
       backward chain over the IMPORT_LIBRARIES block: seed lib+4 with h,
       then h = SHA1(lib[4 : 4+import_size]).
  3. header hash     -> imageInfo.headerHash
       SHA1( security[allowedMediaTypes .. sizeOfHeaders] || header[0 .. sec_off+8] )
  4. signature       -> imageInfo.signature (0x100 bytes)
       hash = XeCryptRotSumSha(imageInfo[0x100 : infoSize])
       F    = int(perqw(XeCryptBnQwBeSigFormat(hash, salt)))          # big-endian
       sig  = ( (2^((e-1)<<11) mod N) * F )^D  mod N                  # e=3
       stored little-qword-first, each qword big-endian ("BnQwNe").

The RSA math needs no bignum port: XeCrypt's NeModExp is a plain modexp, and the
2^((e-1)<<11) factor is exactly the Montgomery R^2 the verify path divides back
out, so Python's pow() reproduces the stored bytes exactly.

The debug key pair is the well-known devkit key (public knowledge, shipped in
every XEX tool); retail cannot be signed (no private key exists publicly).

Usage:
    python tools/xex_debugsign.py <in.xex> [-o <out.xex>]   # default: in place
    from xex_debugsign import debug_sign; signed = debug_sign(xex_bytes)
"""
import argparse
import hashlib
import struct
import sys

# ---- devkit debug key (de-obfuscated from XexTool XexData.cpp) ---------------
# 2048-bit modulus / private exponent, public exponent 3. N == the debug public
# modulus. Numbers are the plain integers; the byte<->int convention ("BnQwNe":
# limb 0 least-significant, each 8-byte limb big-endian) is applied at the edges.
_N = 0xD7492F13B1C17FBF2C5B33E51CFA0002BB9D881671648107A6E9A7C278F5C0B8D4B7419B0A9960027F95271ACC7BAC2916601A8AC74B6893ED8C08ED2513CCBB9EC2A4CFB3ECC8059F18D04DADA56EFCB0484CF99E6FA4764054226544102AD2D587667BF2289CB37207DA63AAF33EED1EBCD83D91A7897C664E8BD022ABDB90EFA039EA1CF948667DBC4559D0E3F29744F48DC58451C81B4E3A8142C56EB949B0E0F51A8E8039C2B8D9A3F13B6E71DB56910FF8E8BD904DCCBED0C5A743AA6C4C5D8565988C4C6BB8705CC0BE8F1B3D6D5D97FD7330763AE1767061E6BE93273E0670624AA2AD9984E7AB2EE4DBAE1E642F4E6C7399ACE5C91C3577C8BFA06B
_D = 0x8F861F627680FFD4C83CCD4368A6AAAC7D13B00EF64300AFC49BC52C50A3D5D08DCF81120710EAAC550E1A11DDA7C81B644011B1DA3245B7F3B2B09E18B7DDD269D7188A77F33003BF65E033C918F4A8758588A6699FC2F9803816EE2D601C8C8E5A4452A1706877A15A9197C74CD49E147DE57E611A5BA844345D356C72925F664E995622C73C3D9B7038113E9847449E2F660CCDC58A26974F6B9C4222E98E1A553EEB4416C0445DA709AC06807D9E91293542C4CA53310D5E26331F791FE6EBB0D0324922713486EBD1220C81729C6B908BE936A44D55169268EBB10F12DE4FAE198393352901BC167F442D33563A3C9EEF40E6DDAD8D0862F50F4D47CC5B
_E = 3
_CQW = 32                                   # 2048-bit == 32 qwords
_SALT = b"XBOX360XEX"                        # XEX_SALT_XEX (non-revocation)

# XexSecurityInfo / XexHvImageInfo field offsets, relative to the imageInfo start
# (which is securityInfoOffset + 8, after {headerSize, imageSize}).
_II_SIGNATURE = 0x000     # [0x100]
_II_INFOSIZE = 0x100
_II_IMAGEHASH = 0x10C     # [0x14]
_II_IMPORTCOUNT = 0x120
_II_IMPORTHASH = 0x124    # [0x14]
_II_HEADERHASH = 0x15C    # [0x14]
_II_END = 0x174
_SI_SECTIONCOUNT = 0x180  # relative to securityInfoOffset
_SI_SECTIONS = 0x184
_SECTION_STRUCT = 24      # XexHvSectionInfo: u32 (pages<<4|type) + u8[20] hash
_KEY_IMPORT_LIBRARIES = 0x000103FF


# ---- XeCrypt primitives (ported from XeCrypt) -------------------------------

_MASK64 = (1 << 64) - 1


def _rotsum(data):
    """XeCryptRotSum over a qword-multiple buffer; returns the 32-byte state."""
    qw1 = qw2 = qw3 = qw4 = 0
    for i in range(len(data) // 8):
        t = int.from_bytes(data[i * 8:i * 8 + 8], "big")   # XeCryptLoadQuad (BE)
        t2 = (t + qw2) & _MASK64
        qw2 = 1 if t2 < t else 0
        qw4 = ((~t & _MASK64) + qw4 + 1) & _MASK64
        qw1 = (qw2 + qw1) & _MASK64
        qw2 = ((t2 << 29) & 0xFFFFFFFFE0000000) | ((t2 >> 35) & 0x1FFFFFFF)
        b = 1 if qw4 > t else 0
        qw3 = ((~b & _MASK64) + qw3 + 1) & _MASK64
        qw4 = ((qw4 << 31) & 0xFFFFFFFF80000000) | ((qw4 >> 33) & 0x7FFFFFFF)
    return b"".join(x.to_bytes(8, "big") for x in (qw1, qw2, qw3, qw4))


def _rotsumsha(inp):
    """XeCryptRotSumSha: SHA1( rs || rs || inp || ~rs || ~rs ), rs = rotsum."""
    rs = _rotsum(inp)
    inv = bytes((~c) & 0xFF for c in rs)
    h = hashlib.sha1()
    h.update(rs)
    h.update(rs)
    h.update(inp)
    h.update(inv)
    h.update(inv)
    return h.digest()


def _rc4(key, data):
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) & 255
        S[i], S[j] = S[j], S[i]
    out = bytearray()
    i = j = 0
    for b in data:
        i = (i + 1) & 255
        j = (j + S[i]) & 255
        S[i], S[j] = S[j], S[i]
        out.append(b ^ S[(S[i] + S[j]) & 255])
    return bytes(out)


def _perqw(b):
    """Reverse the byte order within each qword (XeCrypt BnQwBeBufSwap)."""
    return b"".join(bytes(reversed(b[i:i + 8])) for i in range(0, len(b), 8))


def _sig_format(hsh, salt):
    """XeCryptBnQwBeSigFormat: build the 0x100 padded/masked signature block."""
    sig = bytearray(0x100)
    sig[0xE0] = 0x01                     # bOne
    sig[0xE1:0xEB] = salt                # abSalt[10]
    sig[0xFF] = 0xBC                     # bEnd
    ab = hashlib.sha1(bytes(sig[0:8]) + hsh + salt).digest()
    sig[0xEB:0xFF] = ab                  # abHash[20]  (the RC4 key, kept in clear)
    sig[0:0xEB] = _rc4(ab, bytes(sig[0:0xEB]))
    sig[0] &= 0x7F
    return _perqw(bytes(sig))            # BnQwBeBufSwap


def _qwne_to_int(b):
    """BnQwNe buffer -> int: limb 0 least-significant, each 8-byte limb big-endian."""
    return int.from_bytes(b"".join(b[i:i + 8] for i in range(len(b) - 8, -1, -8)), "big")


def _int_to_qwne(x, nbytes=0x100):
    be = x.to_bytes(nbytes, "big")
    return b"".join(be[i:i + 8] for i in range(nbytes - 8, -1, -8))


def _rsa_debug_sign(region):
    """Produce the 0x100-byte debug signature over the RotSumSha'd `region`."""
    h = _rotsumsha(region)
    fmt = _sig_format(h, _SALT)
    F = int.from_bytes(_perqw(fmt), "big")     # undo the format's final swap, big-endian
    r2 = pow(2, (_E - 1) << 11, _N)            # == Montgomery R^2 for e=3
    psig = (r2 * F) % _N
    return _int_to_qwne(pow(psig, _D, _N))


# ---- XEX2 hash chain --------------------------------------------------------

def debug_sign(xex):
    """Return `xex` (bytes) debug-signed: fills the section/import/header hashes
    and the RSA signature, matching XexTool `-m d`. Requires an uncompressed,
    unencrypted XEX whose stored basefile is the full image (what elf2xex emits).
    """
    x = bytearray(xex)
    if x[:4] != b"XEX2":
        raise ValueError("not a XEX2 file")
    size_of_headers = struct.unpack_from(">I", x, 0x08)[0]
    sec = struct.unpack_from(">I", x, 0x10)[0]
    n_entries = struct.unpack_from(">I", x, 0x14)[0]
    image_size = struct.unpack_from(">i", x, sec + 4)[0]
    ii = sec + 8                                   # imageInfo start
    info_size = struct.unpack_from(">I", x, ii + _II_INFOSIZE)[0]
    sec_count = struct.unpack_from(">i", x, sec + _SI_SECTIONCOUNT)[0]

    # the decompressed basefile is the image; elf2xex stores it whole, uncompressed
    basefile = bytes(x[size_of_headers:size_of_headers + image_size])
    if len(basefile) < image_size:
        basefile = basefile + b"\0" * (image_size - len(basefile))
    total_pages = 0
    descs = []
    for i in range(sec_count):
        o = sec + _SI_SECTIONS + i * _SECTION_STRUCT
        tc = struct.unpack_from(">I", x, o)[0]
        pages = tc >> 4
        descs.append((o, tc, pages))
        total_pages += pages
    page_size = image_size // total_pages if total_pages else 0

    # 1) section hashes (backward chain) -> descriptor hashes + imageHash
    h = b"\0" * 20
    left = image_size
    for i in range(sec_count - 1, -1, -1):
        o, tc, pages = descs[i]
        x[o + 4:o + 24] = h                        # store running hash in this descriptor
        hsz = pages * page_size
        left -= hsz
        block = basefile[left:left + hsz]
        h = hashlib.sha1(block + struct.pack(">I", tc) + h).digest()
    x[ii + _II_IMAGEHASH:ii + _II_IMAGEHASH + 20] = h

    # 2) import hashes (backward chain) -> lib digests + importHash + importCount
    imp_off = None
    for i in range(n_entries):
        k, v = struct.unpack_from(">II", x, 0x18 + i * 8)
        if k == _KEY_IMPORT_LIBRARIES:
            imp_off = v
            break
    if imp_off is not None:
        name_size = struct.unpack_from(">I", x, imp_off + 4)[0]
        lib_count = struct.unpack_from(">I", x, imp_off + 8)[0]
        libs_base = imp_off + 12 + name_size
        h = b"\0" * 20
        for lib_num in range(lib_count - 1, -1, -1):
            p = libs_base
            for _ in range(lib_num):
                p += struct.unpack_from(">I", x, p)[0]
            count = struct.unpack_from(">H", x, p + 0x26)[0]
            import_size = 0x24 + count * 4
            x[p + 4:p + 4 + 20] = h                # seed next_import_digest
            h = hashlib.sha1(bytes(x[p + 4:p + 4 + import_size])).digest()
        x[ii + _II_IMPORTHASH:ii + _II_IMPORTHASH + 20] = h
        struct.pack_into(">I", x, ii + _II_IMPORTCOUNT, lib_count)

    # 3) header hash: security[allowedMediaTypes..end] then header[0..sec+8]
    first = bytes(x[sec + _II_END + 8:size_of_headers])   # allowedMediaTypes = sec+8+0x174
    second = bytes(x[0:sec + 8])
    x[ii + _II_HEADERHASH:ii + _II_HEADERHASH + 20] = hashlib.sha1(first + second).digest()

    # 4) RotSumSha over imageInfo[0x100:infoSize], then RSA debug sign
    region = bytes(x[ii + 0x100:ii + info_size])
    x[ii + _II_SIGNATURE:ii + _II_SIGNATURE + 0x100] = _rsa_debug_sign(region)
    return bytes(x)


def verify_signed(xex):
    """Return [] if `xex` is validly debug-signed, else a list of problems.

    A correctly signed XEX is a fixed point of debug_sign (every hash and the
    signature derive only from the basefile and the non-hash header bytes), so
    re-signing must reproduce it exactly; the signature must also be non-zero
    (a zero signature is the retail classification a kit rejects)."""
    x = bytes(xex)
    problems = []
    try:
        sec = struct.unpack_from(">I", x, 0x10)[0]
        ii = sec + 8
        if not any(x[ii:ii + 0x100]):
            problems.append("signature is zero (unsigned / retail-classified)")
        resigned = debug_sign(x)
    except Exception as e:                      # noqa: BLE001 - report, don't raise
        return ["not a signable XEX2: %s" % e]
    if resigned != x:
        for name, off, ln in (("imageHash", ii + _II_IMAGEHASH, 20),
                              ("importHash", ii + _II_IMPORTHASH, 20),
                              ("headerHash", ii + _II_HEADERHASH, 20),
                              ("signature", ii + _II_SIGNATURE, 0x100)):
            if x[off:off + ln] != resigned[off:off + ln]:
                problems.append("%s does not match a fresh sign" % name)
    return problems


def main():
    ap = argparse.ArgumentParser(description="Debug-sign a XEX2 in place")
    ap.add_argument("xex")
    ap.add_argument("-o", "--out", default=None, help="output (default: sign in place)")
    ap.add_argument("--verify", action="store_true",
                    help="check the XEX is validly signed instead of signing it")
    args = ap.parse_args()
    if args.verify:
        problems = verify_signed(open(args.xex, "rb").read())
        if problems:
            print("NOT validly signed: " + "; ".join(problems))
            return 1
        print(f"{args.xex}: validly debug-signed")
        return 0
    signed = debug_sign(open(args.xex, "rb").read())
    with open(args.out or args.xex, "wb") as f:
        f.write(signed)
    print(f"debug-signed {args.out or args.xex}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
