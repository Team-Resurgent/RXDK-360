# Debug info and xbdm debugging

## The format question

Microsoft's native 360 flow is PPC **PE/COFF + PDB**: `cl.exe` emits PPC COFF
objects, the linker produces a PPC PE, `imagexex` wraps it in a XEX, and the
XDK debugger (Visual Studio / xbWatson, over xbdm) symbolicates from the PDB.
PDB is tied to CodeView and the COFF/PE world.

RXDK-360 produces **ELF**, because LLVM has no PPC COFF backend -- the founding
premise of the whole toolchain. clang emits **DWARF** for compiled code and lld
preserves it. So we do not get MS-compatible PDBs; that door is closed by the
same reason we chose ELF.

## Why that is fine

xbdm is a low-level monitor. Its wire protocol deals in **addresses** -- read
and write memory, set breakpoints, read registers, control execution. It does
not consume symbols. The mapping from address to source line and variable
happens **host-side**, in whatever debugger drives the protocol.

So source-level debugging over xbdm needs only a host that (a) reads our debug
format and (b) speaks xbdm's address protocol. The symbol format is our choice.

## The plan: reuse the RXDK debugger architecture

RXDK (the Xbox 1 toolchain) already ships this: a custom DAP debugger with its
own managed symbol reader, value visualizers, and locals/watch over the debug
bridge. Xbox 1 is x86 PE/COFF, so there clang `-gcodeview` + `lld-link` yield
real PDBs read by `Rxdk.Pdb`.

For 360-ELF the same DAP architecture points at a **DWARF** reader instead of a
PDB reader. Everything downstream -- breakpoint placement, stepping, stack
unwinding, variable inspection -- is unchanged, because it was always driving
xbdm by address and symbolicating on the host.

## Options, in order of preference

1. **DWARF + the RXDK DAP** (preferred). clang already emits DWARF; write or
   reuse a DWARF reader behind the existing DAP. No new file format, no lossy
   conversion, and DWARF is richer than what CodeView carries.
2. **DWARF -> PDB conversion**, only if the stock MS tools (retail VS360) must
   be used. A converter is possible but is extra surface area and gains nothing
   the DAP path does not already give.

## Library symbols

The translated MS libraries currently drop their `.debug$S`/`.debug$T`
(CodeView) sections -- see [coff-translation.md](coff-translation.md). That
costs library-level symbols, not our own code's. If library symbolication is
ever wanted, those sections could be converted to DWARF during translation, but
it is not needed for debugging title code and is deferred.
