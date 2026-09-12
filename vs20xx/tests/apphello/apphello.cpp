// Minimal RXDK-360 Xbox 360 title: proves the modern-VS build chain runs the
// XDK cl.exe -> link.exe -> imagexex.exe and emits a bootable .xex.
#include <xtl.h>

extern "C" int DbgPrint(const char *, ...);

void __cdecl main()
{
    DbgPrint("RXDK-360: hello from a title built by modern Visual Studio\n");
}
