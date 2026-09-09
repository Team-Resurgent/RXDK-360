# ELF -> XEX packing

`tools/elf2xex.py` wraps a linked PPC32 ELF executable in the XEX2 container the
360 loader and xenia expect. The first cut emits an uncompressed, unencrypted
devkit-style XEX -- the form that loads without a signature.

## What the loader requires

xenia's `XexModule::ReadPEHeaders` (and the real console loader) treats the XEX
**basefile as a PE image**, not a flat blob. It maps the basefile contiguously
at the load address and then reads a PE from offset 0:

- DOS header with `MZ`, `e_lfanew` -> NT headers
- `PE\0\0`, `FileHeader.Machine == 0x01F2` (POWERPCBE), the `32BIT_MACHINE`
  characteristic, `SizeOfOptionalHeader == 224`
- `OptionalHeader.Magic == 0x10B` (PE32), `Subsystem == 14` (XBOX)
- a section table; each section's data is taken from its RVA in the basefile

So the packer builds the basefile as `[PE headers at RVA 0][sections at their
RVAs]`, assembled from the ELF's allocatable **sections** (not its PT_LOAD
segments, whose first page is the ELF's own headers).

## Endianness, verified on a real basefile

The 360 PE headers are **little-endian**, even though the code they describe is
big-endian. Confirmed by extracting a retail basefile with XexTool:
`e_lfanew = 0xF0` little-endian, machine bytes `f2 01` = 0x01F2, section count
`08 00` = 8. So `elf2xex.py` writes every PE header field little-endian and
leaves section contents (the PPC code) big-endian.

## Layout produced

- image base from the ELF's lowest PT_LOAD vaddr (what `--image-base` set), not
  the lowest section (lld can float the first section above the base)
- one PE section per allocatable ELF section, RVA = vaddr - base,
  characteristics from the ELF flags (X -> CODE|EXECUTE, W -> WRITE)
- XEX images page in **64KB** units: `SizeOfImage` and the security-info
  section table are 64KB-aligned
- optional headers: entry point, image base, original base (inline-value
  keys); basefile format (uncompressed/unencrypted) as an offset block
- security info with the load address and one section spanning the image;
  hashes and the image key are left zero (a dev kit does not check an unsigned
  image)

## Verified

XexTool (byte-exact, used here as the reader) parses the output cleanly:

```
Basefile Info
  Load Address:       82000000
  Entry Point:        8201011C
  Image Size:            20000
Sections
    0) 82000000 - 82020000 : Data
```

`XexTool -b` extracts a basefile that is a valid PE (MZ / PE / machine 0x01F2 /
subsystem 14), so it satisfies every check `ReadPEHeaders` makes.

## Loading under xenia: how far it gets

Tested against the user's canary build (`canary_experimental@99b3ccad2`), which
loads and runs a real XDK sample fine, so xenia itself is a good oracle. Each
fix moved the failure later, and each was a real packer correctness bug:

1. **rejected outright** -- basefile was a flat image, not a PE. Fixed by
   synthesising the little-endian PPC PE (above).
2. **"load failed with code 3"** -- `is_valid_executable` wants the first dword
   to be `0x905A4D`; the DOS header must be `MZ\x90\x00`, not `MZ\0\0`.
3. **"conflicting address range"** at 0x82000000 -- that region conflicted in
   this xenia; the real sample loads at 0x92000000, so we link there.
4. **memcpy overrun in the loader** -- the page descriptors counted 64KB pages
   but 0x92000000 pages in xenia's 4KB-page heap, so the reservation was 8x too
   small and the basefile copy overran it. Fixed by `page_size_for(base)`.

After those, xenia parsed every header (verified field-for-field against its
`xex2_header` / `xex2_security_info` / opt-header structs), allocated the image,
and copied the basefile -- then access-violated at a fixed offset during load.

5. **the CODE page descriptor.** Building xenia Release with symbols and
   symbolising the fault (`xenia_canary.exe+273D6A` -> `XXH3_update` <-
   `user_module.cc:1160`) named it exactly. xenia computes a per-title hash
   over the code section by scanning the page descriptors for the first and
   last page whose `info` is `XEX_SECTION_CODE`. Our packer marked the whole
   image `DATA`, so the scan found no CODE page, returned `UINT32_MAX` for both
   ends, and `start = base + UINT32_MAX * page_size` fed a wild range into the
   hash, which read unmapped memory. Marking the descriptor CODE fixes it.

**Result: our XEX loads and executes.** With the CODE fix, xenia loads the
image without crashing and runs the guest -- confirmed by the emulator spinning
at ~116% CPU on the test's `for(;;){}` entry (a guest thread executing the
JITted PPC loop). The whole pipeline is proven end to end with our own tools:
COFF -> ELF translation, lld link, ELF -> XEX pack, and xenia loads and runs it.

## How to build xenia with symbols (for future crash work)

The shipped canary build has no PDB. Build one from `D:\Git\xenia-canary`:

```
cmake --build build/vs-x64 --config Release --target xenia-app --parallel
```

Release has symbols and no ASan (the Checked config enables ASan, which aborts
on a pre-existing font-init overflow before the title loads). Without a Vulkan
SDK the SPIR-V shader steps fail; the committed bytecode under
`src/xenia/gpu/shaders/bytecode/vulkan_spirv` is already present, so a small
guard in `tools/build/compile_shader_spirv.py` (skip when glslang is missing and
the output exists) lets it link. To read a crash address without dismissing the
modal dialog, have `HostExceptionReport::DisplayExceptionMessage` also write
`Report_Scratchbuffer` to a file. Symbolise with:

```
llvm-symbolizer --obj=xenia_canary.exe <ImageBase + RVA>   # ImageBase 0x140000000
```

## Kernel imports: a title that prints through DbgPrint

`elf2xex.py --import LIB:sym1,sym2,...` builds the IMPORT_LIBRARIES header. Each
function import is two records already placed in the image and named as ELF
symbols: a 4-byte **variable** record (`0x00000000 | ordinal`) and a 16-byte
**thunk** (`0x01000000 | ordinal` in its first word). At load the kernel reads
the record bytes, rewrites each thunk to `sc 2` (a syscall), and resolves the
ordinal; a title calls `bl <thunk>` to reach the kernel function.

Proven end to end with a hand-written PPC title (`build/import/hello_import.s`)
that calls `xboxkrnl` DbgPrint (ordinal 3) and HalReturnToFirmware (0x28):

```
(DbgPrint) RXDK-360: hello from our own toolchain via DbgPrint
HalReturnToFirmware(00000000)
Game requested a hard poweroff via HalReturnToFirmware
```

xenia loads it with **100% import resolution**, runs our entry, prints our
message through the kernel, and exits cleanly. The whole pipeline -- translate
the MS libraries, link, pack, load, run, call the kernel, see output -- works
with our own tools.

**Layout gotcha:** an import thunk must not sit immediately after the entry
code. xenia declares each thunk as its own (extern) function; if one abuts the
entry's basic block, the entry-function analysis collides with it and the entry
is treated as an "undefined extern call" and never runs. A small gap between
the entry code and the thunks avoids it.

## Compiled C calling the kernel

`gen_import_stubs.py` closes the loop from hand-written asm to compiled C. It
reads the undefined symbols from a compiled title object, resolves each ordinal
from the XDK import libraries (`xboxkrnl.lib` etc., whose short-import members
carry the real console ordinals), and emits the linkable thunks plus a manifest
for `elf2xex --import-manifest`. A title written in C:

```c
extern int  DbgPrint(const char* format, ...);
extern void HalReturnToFirmware(unsigned int routine);
void _start(void) {
    DbgPrint("RXDK-360: compiled C calling the kernel via DbgPrint\n");
    HalReturnToFirmware(0);
    for (;;) {}
}
```

compiles, links against the generated stubs, packs, and runs -- xenia logs
`(DbgPrint) RXDK-360: compiled C calling the kernel via DbgPrint` and exits.

A linker script gives the layout the packer needs: sections start at base +
0x1000 (leaving room for the PE headers) and the import thunks sit in their own
`.kthunks` section past a small gap, so no thunk abuts the entry's basic block.

## Linking against the translated Microsoft libraries

The final rung: a C title that calls a **real XDK library function**, linked
against the libraries we translated from the shipped `.lib`s, not against
hand-written asm. `coff2elf.py` turns `xapilib.lib` -> `xapilib.a` and (the
CRT) `libcMT.lib` -> `libcMT.a` (687 objects: `__savegprlr_*`, `_blkmov`,
`memcpy`, `malloc`, ...). A title:

```c
extern void OutputDebugStringA(const char* str);
extern void HalReturnToFirmware(unsigned int routine);
void _start(void) {
    OutputDebugStringA("RXDK-360: calling xapilib OutputDebugStringA from C\n");
    HalReturnToFirmware(0);
    for (;;) {}
}
```

links `apititle.o + xapilib.a + libcMT.a` with only **two** symbols left
undefined -- `RtlInitAnsiString` and `HalReturnToFirmware`, the actual kernel
imports; everything else (`OutputDebugStringA`, `RtlOutputDebugString`, the CRT
startup helpers) is satisfied from the translated archives. `gen_import_stubs.py
--names RtlInitAnsiString,HalReturnToFirmware` generates the thunks + manifest,
the link completes, and the pack + load under xenia resolves both imports and
runs. xenia logs the clean exit through `HalReturnToFirmware`:

```
xboxkrnl.exe - 4 imports      (F ... 12C  RtlInitAnsiString, 028  HalReturnToFirmware)
i> F8000008 HalReturnToFirmware(00000000)
!> F8000008 Game requested a hard poweroff via HalReturnToFirmware
```

So the translated MS libraries link and execute: the guest ran real retail
xapilib code and returned to the kernel through a real kernel import.

**Why the OutputDebugStringA text does not appear in xenia (and why that is
correct).** Disassembling the translated `RtlOutputDebugString` (what
`OutputDebugStringA` tail-calls) shows a faithful copy of the retail routine:

```
lhz  r4, 0(r3)      ; ANSI_STRING.Length
lwz  r3, 4(r3)      ; ANSI_STRING.Buffer
b    +8
tw   31, r0, r0
twi  31, r0, 0x14   ; 0x0FE00014 -- the 360 hypervisor debug-print trap
blr
```

Retail `OutputDebugString` emits through the `twi 31,r0,0x14` debug trap, not
through a kernel export. xenia JITs that instruction via the generic
`InstrEmit_trap` path (`ppc_emit_control.cc`) -- it is a plain guest trap to
xenia, never decoded as "print this string." So the byte-for-byte-correct
retail code runs, but its output surfaces only on a console/debug monitor that
decodes the trap. To see debug text **in xenia**, call the `DbgPrint` export
(proven above); the trap route is a xenia display gap, not a translation bug.

**Gotcha -- `--gc-sections` drops the import records.** The `__imp_<name>`
variable records in `.kvars` have no code referencing them (only the thunk is
reached, via `bl`), so `--gc-sections` garbage-collects `.kvars` and the packer
then can't find `__imp_RtlInitAnsiString` in the ELF. Wrap the import sections
in `KEEP()` in the linker script (`.kvars : { KEEP(*(.kvars)) }`,
`.kthunks : ALIGN(16) { KEEP(*(.kthunks)) }`) so GC retains them.

## Per-section page descriptors (writable data)

The first cut marked the whole image with one `CODE` page descriptor. xenia
(and the console) protect memory from the descriptors: with the default
`writable_code_segments=false`, `CODE` and `READONLY_DATA` pages map **read-only**
and only `DATA` pages map **read-write** (`xex_module.cc`, "Setup memory
protection"). So under one `CODE` span every page was read-only and a title that
wrote a global faulted.

`build_page_descriptors` now walks the image a page at a time and marks each page
from the sections that occupy it: a page with a writable section -> `DATA`
(read-write), else a page with executable code -> `CODE`, else `READONLY_DATA`.
At least one page is kept `CODE` for xenia's per-title code hash
(`user_module.cc` `find_code_section_page` reads a wild range if it finds none).

Page granularity is coarse (64KB below 0x90000000), so the writable region must
start on its own page or it shares a `CODE` page and stays read-only. The linker
script aligns it:

```
  . = ALIGN(0x10000);          /* writable region on its own 64KB page */
  .data : { *(.data*) }
  .bss  : { *(.bss*) *(COMMON) }
```

**Proven.** A title that writes `.data` and `.bss` (`build/ctitle/apidata.c`),
packed with the aligned layout, produces two descriptors and runs:

```
wrote apidata.xex: image 131072 bytes (2 pages)
  pages: 1xCODE, 1xRWDATA
...
Sections:
    0 CODE      1 pages    82000000 - 82010000
    1 RWDATA    1 pages    82010000 - 82020000
(DbgPrint) RXDK-360: wrote g_counter=42 g_bss[0]=4200 (writable data works)
Game requested a hard poweroff via HalReturnToFirmware
```

The writes land (g_counter 41 -> 42, g_bss[0] = 4200) and the title exits
cleanly. The same object linked *without* the alignment packs as one forced
`CODE` page (the packer prints a note) and its main thread dies instead of
printing -- confirming the read-write mapping is what the descriptors buy.

## Next

- fold the linker-script layout into a reusable link step, and eventually build
  the patched clang so C/C++ compiles with the MS ABI (not zig's PPC EABI)

## Production home

`elf2xex.py` is the research prototype -- Python is fast to iterate while the
format is being pinned down, and XexTool is verifying every output. Once the
format is settled (imports wired, loads under xenia, boots on hardware), the
natural home is **XexTool itself**: it already reads and writes XEX and carries
the header/basefile machinery (`XexWriter`, `XexHeader`, `XexPacker`), so an
ELF-input packing mode would reuse that C++ code rather than reimplement it.
The Python stays as the reference/spec.
