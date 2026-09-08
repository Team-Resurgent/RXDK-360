# Tooling decisions

Standing decisions about what we build and in what language, so they do not have
to be re-litigated.

## Language

**New tools are C#.** Anything we create from scratch should end up as a C#
project. The Python under `tools/` is expedient rather than final:

| tool | status |
|---|---|
| `tools/gentests.py` | working; **to be ported to C#** |
| `tools/verify_abi.py` | working; **to be ported to C#** |
| `tools/abi_scan.py`, `header_scan.py`, `surface_scan.py` | one-shot analysis, ported only if they become part of a build |

**Existing C tools stay C.** XexTool and anything of that lineage remain C/C++
with Visual Studio projects; there is no value in rewriting them.

## XexTool V2

A tidied XexTool goes in `D:\Git\XexTool-V2`, keeping C/C++ and VS projects.
What the current tree (`D:\Git\XexTool`) actually contains:

| component | files | used? | plan |
|---|---|---|---|
| `XexTool` | 62 | the tool itself | keep |
| `mbedtls` | 449 | **no** | **drop** |
| `XeCrypt` | 14 | linked | **submodule** |
| `ldic` | 47 | linked, LZX codec | **replace with mspack** |
| `tinyxml` | 9 | linked | keep |

Scaffolded at `D:\Git\XexTool-V2`: sources gathered into `src/`, XeCrypt wired
as a submodule, tinyxml vendored, mbedtls removed. Build files and the ldic
replacement remain.

### mbedtls is dead weight

Nothing includes it and no project file references it -- `XexTool.vcxproj`
links `XeCrypt`, `ldic` and `tinyxml` only. Removing it takes roughly three
quarters of the source tree with it.

### XeCrypt as a submodule

Already cloned at `D:\Git\XeCrypt`, and it is submodule-ready: `CMakeLists.txt`,
`Makefile`, and vcxproj files for 2013 / 2015 / 2019 / 2026.

Note the remote there is `github.com/cOzInABox/XeCrypt`, not
`github.com/team-Resurgent/XeCrypt` as intended -- worth confirming which is the
canonical one before wiring the submodule.

### ldic replacement -- library not yet identified

`ldic` is a full LZX codec with both `encoder/` and `decoder/` directories.
XexTool only ever calls the decoder: `LdicCreateDecompression`,
`LdicSetWindowData`, `LdicDecompress`, `LdicResetDecompression` and
`LdicDestroyDecompression`, from `XexPacker.cpp` and `XexPatcher.cpp`. So the
tool unpacks XEXs but never creates compressed ones.

That splits the requirement:

- preserving current behaviour needs only an LZX **decompressor**;
- creating compressed XEXs, which RXDK-360 will need to turn a linked image
  into a XEX, needs an LZX **compressor** -- the harder half to source.

`github.com/fhanau/mspack` turned out to be a mass-spectrometry data
compressor, not Stuart Caie's libmspack; the names collide. Its sources are
`SHA1.cpp`, `tinyxml2.cpp`, `msprint.cpp` and it contains no LZX at all. The
right library still needs to be chosen.

## Test artefacts

Xenia's PowerPC test runner needs a `.bin` and `.map` per test, normally built
by a custom binutils via `xb gentests`. `tools/gentests.py` produces them with
our own clang and lld instead, so the toolchain under test is also the one
building the tests. That property should survive the C# port.
