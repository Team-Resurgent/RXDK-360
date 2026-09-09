#include "rt.h"

// A prebuilt XDK library function (xapilib), linked against the MODERN runtime:
// its internal CRT calls (memcpy, the MS register helpers) must resolve from our
// picolibc + glue, and it must run and return correctly.
extern void OutputDebugStringA(const char* s);

void t_apilib(void) {
    OutputDebugStringA("(hello from prebuilt xapilib)\n");
    DbgPrint("apilib: OutputDebugStringA returned\n");
}
