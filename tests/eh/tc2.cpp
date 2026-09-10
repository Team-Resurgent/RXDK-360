extern "C" int DbgPrint(const char *, ...);
struct Guard { int id; ~Guard(); };
Guard::~Guard() { DbgPrint("[T] dtor-%d\n", id); }
extern "C" void rxdk_eh_test2(void) {
    DbgPrint("[T] before-throw\n");
    try {
        Guard g; g.id = 7;
        throw 0x1234;
    } catch (int e) {
        DbgPrint("[T] caught-int %x\n", e);
    }
    DbgPrint("[T] after-catch\n");
}
