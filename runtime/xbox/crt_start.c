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

/* The process heap. The XDK's RtlAllocateHeap-based allocators -- GetProcessHeap,
   and the many shipped libraries that call HeapAlloc(GetProcessHeap(), ...), vcomp
   among them for its OpenMP lock objects -- read the heap handle from the data
   symbol XapiProcessHeap. On console the real XapiInitProcess creates it early with
   RtlCreateHeap; our minimal startup mirrors that here. RtlCreateHeap and
   XapiProcessHeap come from the XDK's own xapilib (translated), so the heap layout
   matches exactly what RtlAllocateHeap/RtlFreeHeap expect -- our own malloc uses
   the kernel pool (ExAllocatePool) and is unaffected. */
extern void *XapiProcessHeap;
extern void *RtlCreateHeap(unsigned flags, void *base,
                           unsigned long reserve, unsigned long commit,
                           void *lock, void *parameters);

static void init_process_heap(void) {
    if (!XapiProcessHeap)
        /* HEAP_GROWABLE (0x2): grows on demand from the reserve. Matching the
           XDK's process-heap creation; sizes are the conventional defaults. */
        XapiProcessHeap = RtlCreateHeap(0x2, 0, 0x40000, 0x10000, 0, 0);
}

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

/* The loader's command line: xboxkrnl exports ExLoadedCommandLine (ordinal
   0x1AE) as a *data* variable -- a char* to the launch command line (empty on a
   normal disc/xex launch, non-empty when a debugger / Remote Reboot / a title
   relaunch passes one). It is imported as a variable (tools/gen_import_stubs.py
   emits it as a .kvars slot the loader patches with the pointer), so reading it
   yields the string, with no xapi initialisation required. NULL if unresolved --
   the tokeniser handles that. */
extern char *ExLoadedCommandLine;

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

/* Build argv from the kernel's loaded command line for main(). Empty on a normal
   launch -> argc 0, argv[0] == NULL. __rxdk_parse_cmdline is also exercised
   directly (with known inputs) by tests/stdlib/t_args.c. */
static int build_args(void) {
    return __rxdk_parse_cmdline(ExLoadedCommandLine, g_cmdline, RXDK_CMDLINE_MAX,
                                g_argv, RXDK_ARG_MAX + 1);
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

/* C standard exit(): run the atexit/static-dtor + .fini_array teardown, trace the
   status the way the console's xapi startup does, and power off. Callable from any
   depth (unlike returning from main). _start routes through it so the normal-return
   and explicit-exit paths are identical. picolibc's exit.c is excluded from the
   build in favour of this one (it drives our __rxdk_run_atexit, not picolibc's
   exitprocs). _Exit()/_exit() (no teardown) remain picolibc's. */
_Noreturn void exit(int code) {
    __rxdk_run_atexit();  /* C++ static-object dtors + atexit, reverse order */
    run_fini();           /* .fini_array (e.g. the stdio flush), runs last */
    DbgPrint("[XAPI RETURN VALUE] %d\n", code);
    HalReturnToFirmware(0);
    for (;;) {}
}

void _start(void) {
    init_process_heap();              /* before init: C++/CRT ctors may allocate */
    run_init();
    int argc = build_args();
    exit(main(argc, g_argv));         /* g_argv is always NULL-terminated */
}
