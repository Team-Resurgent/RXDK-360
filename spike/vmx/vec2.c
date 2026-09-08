#include <vectorintrinsics.h>
typedef __vector4 V;
extern void sinkv(V);
/* callee side: forces the incoming argument registers to be visible */
int def_mixv(int a, V b, float c, V d, int e) { sinkv(b); sinkv(d); return a + e; }
void def_manyv(V a,V b,V c,V d,V e,V f,V g,V h,V i,V j,V k,V l,V m,V n)
{ sinkv(a);sinkv(b);sinkv(c);sinkv(d);sinkv(e);sinkv(f);sinkv(g);
  sinkv(h);sinkv(i);sinkv(j);sinkv(k);sinkv(l);sinkv(m);sinkv(n); }
