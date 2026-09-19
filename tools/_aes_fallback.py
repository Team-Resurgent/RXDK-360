#!/usr/bin/env python3
"""Pure-Python AES-128-CBC encrypt -- the off-Windows fallback for
xex_debugsign's basefile encryption (Windows uses bcrypt via ctypes, which is
far faster). Correct but slow; only used when BCrypt is unavailable.
"""


def _init_sbox():
    p = q = 1
    sbox = [0] * 256
    while True:
        p = p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 0xFF
        if q & 0x80:
            q ^= 0x09
        x = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ \
            ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4))
        sbox[p] = (x ^ 0x63) & 0xFF
        if p == 1:
            break
    sbox[0] = 0x63
    return sbox


_SBOX = _init_sbox()
_RCON = [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36]
_MUL2 = [(_x << 1) ^ 0x1B & 0xFF if _x & 0x80 else _x << 1 for _x in range(256)]
_MUL2 = [((_x << 1) ^ 0x1B) & 0xFF if _x & 0x80 else (_x << 1) & 0xFF for _x in range(256)]
_MUL3 = [_MUL2[_x] ^ _x for _x in range(256)]


def _expand(key):
    w = [list(key[i:i + 4]) for i in range(0, 16, 4)]
    for i in range(4, 44):
        t = list(w[i - 1])
        if i % 4 == 0:
            t = t[1:] + t[:1]
            t = [_SBOX[b] for b in t]
            t[0] ^= _RCON[i // 4 - 1]
        w.append([w[i - 4][j] ^ t[j] for j in range(4)])
    return w


def _encrypt_block(block, w):
    s = [[block[r + 4 * c] for c in range(4)] for r in range(4)]

    def addrk(rnd):
        for c in range(4):
            for r in range(4):
                s[r][c] ^= w[rnd * 4 + c][r]

    addrk(0)
    for rnd in range(1, 10):
        for r in range(4):
            for c in range(4):
                s[r][c] = _SBOX[s[r][c]]
        s[1] = s[1][1:] + s[1][:1]
        s[2] = s[2][2:] + s[2][:2]
        s[3] = s[3][3:] + s[3][:3]
        for c in range(4):
            c0, c1, c2, c3 = s[0][c], s[1][c], s[2][c], s[3][c]
            s[0][c] = _MUL2[c0] ^ _MUL3[c1] ^ c2 ^ c3
            s[1][c] = c0 ^ _MUL2[c1] ^ _MUL3[c2] ^ c3
            s[2][c] = c0 ^ c1 ^ _MUL2[c2] ^ _MUL3[c3]
            s[3][c] = _MUL3[c0] ^ c1 ^ c2 ^ _MUL2[c3]
        addrk(rnd)
    for r in range(4):
        for c in range(4):
            s[r][c] = _SBOX[s[r][c]]
    s[1] = s[1][1:] + s[1][:1]
    s[2] = s[2][2:] + s[2][:2]
    s[3] = s[3][3:] + s[3][:3]
    addrk(10)
    return bytes(s[r][c] for c in range(4) for r in range(4))


def aes128_cbc_encrypt(data, key, iv=b"\0" * 16):
    w = _expand(key)
    out = bytearray()
    prev = iv
    for i in range(0, len(data), 16):
        blk = bytes(a ^ b for a, b in zip(data[i:i + 16], prev))
        prev = _encrypt_block(blk, w)
        out += prev
    return bytes(out)
