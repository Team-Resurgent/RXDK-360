/* ---- callee-saved register discovery: force heavy register pressure ---- */
extern int  g[64];
extern void barrier(void);
int saved_gpr(void)
{   int a=g[0],b=g[1],c=g[2],d=g[3],e=g[4],f=g[5],h=g[6],i=g[7],j=g[8],k=g[9],
        l=g[10],m=g[11],n=g[12],o=g[13],p=g[14],q=g[15],r=g[16],s=g[17];
    barrier();
    return a+b+c+d+e+f+h+i+j+k+l+m+n+o+p+q+r+s; }
extern double dg[32];
double saved_fpr(void)
{   double a=dg[0],b=dg[1],c=dg[2],d=dg[3],e=dg[4],f=dg[5],h=dg[6],i=dg[7],
           j=dg[8],k=dg[9],l=dg[10],m=dg[11],n=dg[12],o=dg[13],p=dg[14],q=dg[15],
           r=dg[16],s=dg[17],t=dg[18],u=dg[19];
    barrier();
    return a+b+c+d+e+f+h+i+j+k+l+m+n+o+p+q+r+s+t+u; }

/* ---- struct by value and struct return, across the size boundaries ---- */
struct S4  { int a; };
struct S8  { int a,b; };
struct S12 { int a,b,c; };
struct S16 { int a,b,c,d; };
struct S24 { int a,b,c,d,e,f; };
extern void t4(struct S4);    extern void t8(struct S8);
extern void t12(struct S12);  extern void t16(struct S16);
extern void t24(struct S24);
void c4(struct S4 s){t4(s);}    void c8(struct S8 s){t8(s);}
void c12(struct S12 s){t12(s);} void c16(struct S16 s){t16(s);}
void c24(struct S24 s){t24(s);}
extern struct S8  r8(void);   extern struct S24 r24(void);
int use_r8(void){ struct S8 s=r8(); return s.a+s.b; }
int use_r24(void){ struct S24 s=r24(); return s.a+s.f; }

/* ---- 64-bit integer arguments and returns ---- */
extern long long ll3(long long a, int b, long long c);
long long cll(long long a,int b,long long c){ return ll3(a,b,c); }

/* ---- varargs callee that actually consumes its arguments ---- */
#include <stdarg.h>
int vsum(int n, ...)
{ va_list ap; int t=0; va_start(ap,n); while(n--) t+=va_arg(ap,int); va_end(ap); return t; }
double vsumd(int n, ...)
{ va_list ap; double t=0; va_start(ap,n); while(n--) t+=va_arg(ap,double); va_end(ap); return t; }
