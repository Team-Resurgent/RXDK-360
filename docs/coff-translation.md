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

   Measured: across all of `xapilib.lib` every REFHI/REFLO has a **zero**
   in-place immediate and a **zero** PAIR field, so the target is carried
   entirely by the symbol (section number + value) and the addend is 0. This
   is the plain `lis rX, sym@ha; addi rX, rX, sym@lo` pattern. The emitter
   still combines the site immediate with the PAIR half generally and warns if
   it ever sees a non-zero addend, so a differently-built library would surface
   loudly rather than translate wrong.

## COFF field endianness

Every scalar in the COFF file/section/symbol/relocation headers is
**little-endian**, even though the target is big-endian PowerPC. Only the
section *contents* (code and data) are big-endian. `coff2elf.py` unpacks all
headers with `<` and passes section bytes through untouched.

## Relocation addend reconstruction

COFF relocations are REL-form (the addend lives in the section bytes at the
site); PPC ELF is RELA (explicit `r_addend`). Each type recovers its addend
differently, all verified on `raiseexception.obj`, which exercises the whole
set:

| type | ELF | addend |
|---|---|---|
| `ADDR32` | `R_PPC_ADDR32` | the 32-bit big-endian word at the site (absolute, additive) |
| `REL24` | `R_PPC_REL24` | `LI + P`. The branch's 24-bit field holds `(addend - P)` as a compile-time placeholder -- with the section based at 0, the only PC-relative value it can encode -- and ELF computes `S + A - P`, so `A = LI + P`. Comes out 0 for a plain call. |
| `REFHI` (+PAIR) | `R_PPC_ADDR16_HA` | combined site-imm/PAIR halves; 0 in practice |
| `REFLO` (+PAIR) | `R_PPC_ADDR16_LO` | combined halves; 0 in practice |

The section bytes are copied verbatim -- the emitted `.text` is byte-identical
to the COFF `.text` -- and the linker overwrites each relocated field from
`S + A`. A non-zero addend on any type is warned about, since none occur in the
shipped libraries and one would mean a convention this hasn't seen.

## Status

- [x] archive + COFF parser, relocation numbering verified on real libs
- [x] input structure surveyed and documented
- [x] ELF32 BE emitter: one code object -> well-formed `.o` (readelf clean)
- [x] section + symbol + relocation mapping (`.text`/`.rodata`/`.data`/`.bss`/`.pdata`)
- [x] REFHI/REFLO/PAIR -> `@ha`/`@lo`, REL24/ADDR32 addend reconstruction
- [x] whole archive translated: 480/480 code objects across xapilib/d3d9/xgraphics,
      zero failures, zero warnings
- [x] repack as System V `.a` with a GNU symbol index -- binutils `ar`/`nm` read it,
      2730 indexed symbols in xapilib, each mapped to its defining member
- [x] import manifest for the short-import members (585 xapilib imports of
      `xam.xex`, with ordinals -- feeds the XEX packer's import table)
- [x] linked with lld (via zig's PPC lld) into a working PPC executable, every
      relocation type confirmed applied correctly

## Linking, and the halfword-offset fix

lld (the one zig bundles, which has full PPC ELF support) reads the translated
`.a`, pulls the right member for each symbol through the archive index, and
applies the relocations. A caller referencing `RaiseException` links to a
complete `ET_EXEC` PowerPC image once the two genuinely-external symbols are
supplied -- `RtlRaiseException` (an `xboxkrnl.exe` import, resolved by the XEX
loader) and `_blkmov` (a CRT helper).

The link test caught a bug that readelf could not: PPC ELF's 16-bit
relocations (`ADDR16_HA`/`ADDR16_LO`) put `r_offset` on the **immediate
halfword** -- two bytes into the big-endian instruction -- whereas COFF
REFHI/REFLO point at the instruction start. Emitting the COFF offset unchanged
made lld write the value over the opcode. With `+2` on those two types, the
linked code reads back exactly right:

```
lis  r9, 0x2       ; RaiseException@ha   (0x201E8 -> 0x0002)
addi r9, r9, 0x1E8 ; RaiseException@lo   -> r9 = 0x201E8 = &RaiseException
bl   _blkmov                             ; REL24, lands on 0x201CC
bl   RtlRaiseException                    ; REL24, lands on 0x201B0
```

The 4-byte relocations (`ADDR32`, `REL24`) sit at the instruction start and
were already right.
