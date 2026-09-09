#include "rt.h"
extern void HalReturnToFirmware(unsigned int routine);
/* Shared entry: run the program's title_main, print a completion sentinel, then
   power off cleanly. The sentinel is the last line the harness expects, so a
   dropped trailing line (xenia's async logger can lose the last line on a fast
   exit) costs the sentinel, not the program's real output; the harness also
   retries a run whose sentinel never arrived. */
void _start(void) {
    title_main();
    DbgPrint("##RXDK-CORPUS-END##\n");
    HalReturnToFirmware(0);
    for (;;) {}
}
