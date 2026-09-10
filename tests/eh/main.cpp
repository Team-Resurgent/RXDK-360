extern "C" void rxdk_eh_test(void);
extern "C" int DbgPrint(const char *, ...);
void main(void){ rxdk_eh_test(); DbgPrint("[T] ALLDONE\n"); }
