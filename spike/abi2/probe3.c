/* Compiler-neutral versions of the probes: no XDK headers, so the same source
   compiles with both cl.exe and clang. */
typedef __builtin_va_list va_list_t;
#define va_start_(ap,l) __builtin_va_start(ap,l)
#define va_arg_(ap,t)   __builtin_va_arg(ap,t)
#define va_end_(ap)     __builtin_va_end(ap)

struct S8 { int a,b; };
struct S24{ int a,b,c,d,e,f; };
int d8 (struct S8 s, int x){ return s.a+s.b+x; }
int d24(struct S24 s,int x){ return s.a+s.f+x; }
long long dll(long long a, int b, long long c){ return a+b+c; }
int vsum(int n, ...)
{ va_list_t ap; int t=0; va_start_(ap,n); while(n-->0) t+=va_arg_(ap,int); va_end_(ap); return t; }
