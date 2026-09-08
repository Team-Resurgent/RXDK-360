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
| `tinyxml` | 9 | linked | keep, or replace |

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

### ldic to mspack

`ldic` is a full LZX codec with both `encoder/` and `decoder/` directories, used
by `XexPatcher.cpp`.

The important detail: **stock libmspack decompresses LZX but does not compress
it**, so it cannot replace `ldic` on its own. `fhanau/mspack` adds LZX
compression, which is exactly what XEX packing needs -- so that fork is the
right target, not upstream libmspack.

## Test artefacts

Xenia's PowerPC test runner needs a `.bin` and `.map` per test, normally built
by a custom binutils via `xb gentests`. `tools/gentests.py` produces them with
our own clang and lld instead, so the toolchain under test is also the one
building the tests. That property should survive the C# port.
