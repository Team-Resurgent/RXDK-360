#include "rt.h"

/* Register a constructor in the MS CRT initializer section, the way a prebuilt
   XDK library does -- distinct from the ELF .init_array. start.c walks .CRT$XC*
   between __xc_a/__xc_z, so if the MS pre-main path works g_ms is set before
   title_main; otherwise it stays 0. */
static int g_ms;
static void ms_ctor(void) { g_ms = 4321; }

__attribute__((section(".CRT$XCU"), used))
static void (*ms_init_ptr)(void) = ms_ctor;

void t_msinit(void) {
    DbgPrint("MS pre-main init: g_ms=%d\n", g_ms);
}
