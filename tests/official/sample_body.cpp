// Shared title body compiled by BOTH the official XDK cl.exe and our clang, to
// compare startup/ABI/xapi behavior. Self-declares its imports so it needs no
// XDK headers (the official wrapper still includes <xtl.h>). The file-scope
// static constructor checks that both runtimes run C++ static init pre-main.
extern "C" int DbgPrint(const char *, ...);
extern "C" unsigned XGetLanguage(void);   // XAPI -> xam.xex import

struct RxdkCmp_Init {
    RxdkCmp_Init() { DbgPrint("[ctor] static init before main\n"); }
};
static RxdkCmp_Init g_rxdk_cmp_init;

extern "C" void rxdk_body(void)
{
    DbgPrint("[main] entered\n");
    DbgPrint("[main] XGetLanguage=%d\n", (int)XGetLanguage());
    DbgPrint("[main] done\n");
}
