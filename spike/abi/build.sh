#!/bin/sh
# ABI probe: compile identical source with the MS Xenon compiler and with zig cc,
# then diff argument placement. Requires the Xbox 360 XDK (Windows) for the MS half.
XEDK="${XEDK:-/c/Program Files (x86)/Microsoft Xbox 360 SDK}"
set -e
"$XEDK/bin/win32/cl.exe" -c -O2 abi.c -Foabi_ms.o >/dev/null
"$XEDK/bin/win32/dumpbin.exe" -DISASM:NOBYTES abi_ms.o > abi_ms.dis
zig cc -target powerpc-freestanding-eabihf -O2 -S abi.c -o abi_zig.s
echo "wrote abi_ms.dis and abi_zig.s"
