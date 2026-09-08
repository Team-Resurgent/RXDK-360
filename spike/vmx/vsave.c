#include <vectorintrinsics.h>
typedef __vector4 V;
extern V vg[40];
extern void barrier(void);
V heavy(void)
{ V a=vg[0],b=vg[1],c=vg[2],d=vg[3],e=vg[4],f=vg[5],g=vg[6],h=vg[7],
    i=vg[8],j=vg[9],k=vg[10],l=vg[11],m=vg[12],n=vg[13],o=vg[14],p=vg[15],
    q=vg[16],r=vg[17],s=vg[18],t=vg[19];
  barrier();
  return __vaddfp(__vaddfp(__vaddfp(__vaddfp(a,b),__vaddfp(c,d)),
                  __vaddfp(__vaddfp(e,f),__vaddfp(g,h))),
         __vaddfp(__vaddfp(__vaddfp(i,j),__vaddfp(k,l)),
                  __vaddfp(__vaddfp(m,n),__vaddfp(__vaddfp(o,p),
                  __vaddfp(__vaddfp(q,r),__vaddfp(s,t)))))); }
