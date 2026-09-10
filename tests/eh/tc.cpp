extern "C" int DbgPrint(const char *, ...);
extern "C" void rxdk_eh_test(void) {
    DbgPrint("[T] before-throw\n");
    try { throw 0x1234; }
    catch (int e) { DbgPrint("[T] caught-int %x\n", e); }
    catch (...)   { DbgPrint("[T] caught-other\n"); }
    DbgPrint("[T] after-catch\n");
}
