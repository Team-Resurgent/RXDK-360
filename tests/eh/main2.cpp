extern "C" void rxdk_eh_test2(void);
extern "C" int DbgPrint(const char *, ...);
void main(void){ rxdk_eh_test2(); DbgPrint("[T] ALLDONE\n"); }
