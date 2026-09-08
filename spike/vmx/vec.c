#include <vectorintrinsics.h>
typedef __vector4 V;
extern int mixv(int a, V b, float c, V d, int e);
extern V   many_v(V a,V b,V c,V d,V e,V f,V g,V h,V i,V j,V k,V l,V m,V n);
int call_mixv(int a, V b, float c, V d, int e) { return mixv(a,b,c,d,e); }
V   call_many_v(V a,V b,V c,V d,V e,V f,V g,V h,V i,V j,V k,V l,V m,V n)
{ return many_v(a,b,c,d,e,f,g,h,i,j,k,l,m,n); }
V   passthru(V a) { return a; }
