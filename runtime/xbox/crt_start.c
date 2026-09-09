/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * Standard title entry: the CRT startup a title gets for free when it defines
 * main() and links libc, mirroring the XDK's XapiTitleStartup. It runs the C++
 * static constructors (.init_array) and the MS CRT initializer table (.CRT$XC*)
 * before main -- so both first-party and prebuilt-MS pre-main init run the way a
 * console title expects -- calls main, then runs the destructors (.fini_array,
 * e.g. the stdio flush), traces main's return value the way the console's xapi
 * startup does, and powers off.
 *
 * This lives in libc.a as its own object, so it is pulled in only when the title
 * has not supplied its own _start (e.g. the corpus driver, which uses its own
 * entry + title_main). ENTRY(_start) in the linker script points here.
 */
extern int  DbgPrint(const char *, ...);
extern void HalReturnToFirmware(unsigned int routine);

typedef void (*init_fn)(void);
extern init_fn __init_array_start[];
extern init_fn __init_array_end[];
extern init_fn __xc_a[];             /* .CRT$XCA .. .CRT$XCZ (MS ctor table) */
extern init_fn __xc_z[];
extern init_fn __fini_array_start[];
extern init_fn __fini_array_end[];

/* The title's entry. Declared with (int, char**) so int main(void),
   void main(void) and int main(int, char**) all link and call correctly; a
   console title has no command line, so argc/argv are 0/NULL. */
extern int main(int argc, char **argv);
extern void __rxdk_run_atexit(void);  /* C++ static dtors + atexit handlers */

static void run_init(void) {
    for (init_fn *f = __init_array_start; f != __init_array_end; ++f)
        (*f)();
    for (init_fn *f = __xc_a; f != __xc_z; ++f)
        if (*f)
            (*f)();
}

static void run_fini(void) {
    /* .fini_array runs in reverse of registration order. */
    for (init_fn *f = __fini_array_end; f != __fini_array_start;)
        (*--f)();
}

void _start(void) {
    run_init();
    int rc = main(0, (char **)0);
    __rxdk_run_atexit();  /* C++ static-object dtors + atexit, reverse order */
    run_fini();           /* .fini_array (e.g. the stdio flush), runs last */
    DbgPrint("[XAPI RETURN VALUE] %d\n", rc);
    HalReturnToFirmware(0);
    for (;;) {}
}
