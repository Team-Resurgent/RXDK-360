#include <stdarg.h>
extern void sink(int);
struct S4{int a;}; struct S8{int a,b;}; struct S12{int a,b,c;};
struct S16{int a,b,c,d;}; struct S24{int a,b,c,d,e,f;};
/* callee side: consume the fields so incoming locations are visible */
int d4 (struct S4 s, int x){ return s.a+x; }
int d8 (struct S8 s, int x){ return s.a+s.b+x; }
int d12(struct S12 s,int x){ return s.a+s.b+s.c+x; }
int d16(struct S16 s,int x){ return s.a+s.b+s.c+s.d+x; }
int d24(struct S24 s,int x){ return s.a+s.f+x; }
struct S8  mk8 (int a,int b){ struct S8  s; s.a=a; s.b=b; return s; }
struct S24 mk24(int a,int b){ struct S24 s; s.a=a; s.f=b; return s; }
long long dll(long long a, int b, long long c){ return a+b+c; }
int vsum(int n, ...)
{ va_list ap; int t=0; va_start(ap,n); while(n-->0) t+=va_arg(ap,int); va_end(ap); return t; }
