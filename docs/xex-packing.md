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

After those, xenia parses every header (verified field-for-field against its
`xex2_header` / `xex2_security_info` / opt-header structs), allocates the image
correctly, and copies the basefile -- then still access-violates at a fixed
offset during load setup, the same offset at either base. It is a deterministic
deref triggered by the image content, but the shipped canary build has no PDB,
so the exact site can't be pinned from here.

The likely cause is that the test image is not a realistic title: a bare
`for(;;){}` with **no imports, no execution-info/TLS/stack-size headers, and
code built for the wrong ABI** (zig's PPC EABI, not the MS convention). Real
titles carry all of those, and xenia's later setup may assume some. Pinning it
needs either a xenia debug build with symbols, or a realistic image.

## Next

- either build xenia from the matching source with symbols to locate the deref,
  or move to a realistic image and see the crash disappear
- wire kernel imports (the import manifest from `coff2elf.py`) into an
  IMPORT_LIBRARIES optional header, plus execution-info / TLS / stack-size
  headers a real title has
- a real entry that calls a debug-print import, to close the boot-and-print loop
  (with `kernel_debug_monitor = true`)

## Production home

`elf2xex.py` is the research prototype -- Python is fast to iterate while the
format is being pinned down, and XexTool is verifying every output. Once the
format is settled (imports wired, loads under xenia, boots on hardware), the
natural home is **XexTool itself**: it already reads and writes XEX and carries
the header/basefile machinery (`XexWriter`, `XexHeader`, `XexPacker`), so an
ELF-input packing mode would reuse that C++ code rather than reimplement it.
The Python stays as the reference/spec.
