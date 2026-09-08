# PPC COFF -> PPC32 ELF translation

The hybrid toolchain reuses the XDK's `lib\xbox\*.lib` instead of reimplementing
them. Those are Microsoft archives of big-endian PowerPC COFF objects (machine
`0x01F2`), which lld cannot read. `tools/coff2elf.py` rewrites each object as a
PPC32 big-endian ELF and repacks the archive as a System V `.a`.

Everything below was measured with `coff2elf.py survey` over `xapilib.lib`,
`d3d9.lib` and `xgraphics.lib` (480 code objects), not assumed.

## What the input actually contains

**Two kinds of archive member:**

- **480 real COFF code objects** (machine `0x01F2`). These carry the code and
  data and are the translator's job.
- **585 short-format import objects** in `xapilib.lib` (`Sig1=0, Sig2=0xFFFF`),
  e.g. `XampXAuthStartup`, `XnpGetXwppRuntimeFilter`. These are dynamic imports
  resolved from other XEX modules at load time. They become undefined symbols
  plus an import manifest the XEX packer wires up -- not the translator's
  concern beyond recording them. `d3d9.lib` and `xgraphics.lib` have none.

**Sections, by frequency:**

| section | disposition |
|---|---|
| `.text` (+ `.text$yc/$yd` COMDAT) | copy to ELF `.text` |
| `.rdata` | copy to ELF `.rodata` |
| `.data` | copy to ELF `.data` |
| `.bss` | ELF `.bss` (NOBITS) |
| `.pdata` / `.xdata` | function/unwind tables -- keep as data sections |
| `.idata$2..$6` | XEX import descriptors -> packer, via the manifest |
| `.CRT$XCU/XCA/XCZ/XIA/XIZ` | CRT init/term -> `.init_array` order preserved |
| `.debug$S/$T/$P` | drop |
| `.drectve` | linker directives; parse defaults, otherwise drop |
| `.XBLD$W/$V` | Xbox build metadata; drop |

## Relocations

The complete set present in real code, with the ELF mapping:

| IMAGE_REL_PPC | value | count | ELF |
|---|---|---|---|
| `REL24`    | 0x06 | 41149 | `R_PPC_REL24` |
| `REFHI`    | 0x10 | 11046 | `R_PPC_ADDR16_HA` (see PAIR) |
| `REFLO`    | 0x11 | 11343 | `R_PPC_ADDR16_LO` |
| `PAIR`     | 0x12 | 22389 | consumed, not emitted |
| `ADDR32`   | 0x02 | 10730 | `R_PPC_ADDR32` |
| `ADDR32NB` | 0x0A |     3 | import descriptors only -> packer |
| `SECREL`   | 0x0B | 48816 | debug only -> drop |
| `SECTION`  | 0x0C | 48816 | debug only -> drop |

Two things the numbering and the counts nailed down, both of which would have
produced silently wrong relocations if assumed:

1. **The high types are 0x10-0x12, not 0x18-0x1A.** Confirmed because the debug
   sections carry SECREL/SECTION, which appear as 0x0B/0x0C, anchoring the rest
   of the winnt.h table.

2. **PAIR follows both REFHI and REFLO**, not just REFHI. `11046 REFHI +
   11343 REFLO = 22389 = PAIR exactly`. Each high or low relocation is
   immediately followed by a PAIR whose value carries the other half of the
   32-bit target, so the high part can be assembled as `@ha` (round-to-even
   adjusted) rather than plain `@h`. The translator reads the PAIR to compute
   the addend, then emits a single ELF relocation.

## COFF field endianness

Every scalar in the COFF file/section/symbol/relocation headers is
**little-endian**, even though the target is big-endian PowerPC. Only the
section *contents* (code and data) are big-endian. `coff2elf.py` unpacks all
headers with `<` and passes section bytes through untouched.

## Status

- [x] archive + COFF parser, relocation numbering verified on real libs
- [x] input structure surveyed and documented
- [ ] ELF32 BE emitter: one code object -> `.o` that lld accepts
- [ ] section + symbol + relocation mapping
- [ ] REFHI/REFLO/PAIR -> `@ha`/`@lo` addend reconstruction
- [ ] repack archive as System V `.a`
- [ ] import manifest for the short-import members
