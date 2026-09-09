#include "rt.h"
extern void HalReturnToFirmware(unsigned int routine);

/* Pre-main: run the C++ static constructors (.init_array) the way a real title's
   xapilib startup does before it reaches the title. The linker script provides
   the bounds. This is the equivalent of the XDK's XapiThreadStartup running the
   CRT init before main; the real xapilib startup will replace this shim later. */
typedef void (*init_fn)(void);
extern init_fn __init_array_start[];
extern init_fn __init_array_end[];

static void run_init_array(void) {
    for (init_fn *f = __init_array_start; f != __init_array_end; ++f)
        (*f)();
}

/* Shared entry: run static ctors, then the program's title_main, print a
   completion sentinel, then power off cleanly. The sentinel is the last line the
   harness expects, so a dropped trailing line (xenia's async logger can lose the
   last line on a fast exit) costs the sentinel, not the program's real output;
   the harness also retries a run whose sentinel never arrived. */
void _start(void) {
    run_init_array();
    title_main();
    DbgPrint("##RXDK-CORPUS-END##\n");
    HalReturnToFirmware(0);
    for (;;) {}
}
