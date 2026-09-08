extern void sink(int*);
extern int  work(void);
/* address-taken parameters force the callee to spill its incoming registers */
int spill(int a, int b, int c, int d) { sink(&a); sink(&b); sink(&c); sink(&d); return a+b+c+d; }
/* varargs callee must home all incoming GPRs */
int vsink(const char* fmt, ...) { return work(); }
