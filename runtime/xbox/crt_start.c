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
   void main(void) and int main(int, char**) all link and call correctly. */
extern int main(int argc, char **argv);
extern void __rxdk_run_atexit(void);  /* C++ static dtors + atexit handlers */

#define RXDK_ARG_MAX     32
#define RXDK_CMDLINE_MAX 256
static char  g_cmdline[RXDK_CMDLINE_MAX];
static char *g_argv[RXDK_ARG_MAX + 1];

/*
 * Split a command line into argv, in place, returning argc. A simple whitespace
 * tokeniser with "quoted arg" support; `cl` is copied into `buf` first (the
 * kernel string may be read-only) and tokenised there, so `argv` points into
 * `buf`. argv[0] is the first token, argv[argc] is NULL. Exposed (non-static) so
 * it can be unit-tested with known inputs, independent of a live command line.
 */
int __rxdk_parse_cmdline(const char *cl, char *buf, int bufsz,
                         char **argv, int argmax) {
    int argc = 0, n = 0;
    char *p;
    if (!cl) { if (argmax > 0) argv[0] = (char *)0; return 0; }
    while (cl[n] && n < bufsz - 1) { buf[n] = cl[n]; ++n; }
    buf[n] = '\0';
    p = buf;
    while (*p && argc < argmax - 1) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"') ++p;
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') ++p;
        }
        if (*p) *p++ = '\0';
    }
    argv[argc] = (char *)0;
    return argc;
}

/* Build argv for main(). The live command line comes from the kernel's
   ExLoadedCommandLine export, but that is a *variable* (data) import, which the
   current import packer (tools/gen_import_stubs.py) emits only as a code thunk --
   so its value is not yet reachable from a title -- and xapilib's GetCommandLineA
   needs xapi state this minimal CRT does not initialise. Until variable imports
   land, hand main a well-formed empty argv (argc 0, argv[0] == NULL): the normal
   console-launch case, where no command line is passed, anyway. __rxdk_parse_cmdline
   above is the part that carries the logic, and it is exercised directly (with
   known inputs) by tests/stdlib/t_args.c, independent of a live command line. */
static int build_args(void) {
    (void)g_cmdline;                 /* reserved for the ExLoadedCommandLine path */
    g_argv[0] = (char *)0;
    return 0;
}

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
    int argc = build_args();
    int rc = main(argc, g_argv);      /* g_argv is always NULL-terminated */
    __rxdk_run_atexit();  /* C++ static-object dtors + atexit, reverse order */
    run_fini();           /* .fini_array (e.g. the stdio flush), runs last */
    DbgPrint("[XAPI RETURN VALUE] %d\n", rc);
    HalReturnToFirmware(0);
    for (;;) {}
}
