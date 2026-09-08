/* leaf function with heavy spill pressure: reveals how far below r1 the ABI allows */
extern int f(int);
int leaf(int a,int b,int c,int d,int e,int g,int h,int i)
{ int v[12]; int k;
  for(k=0;k<12;k++) v[k]=a*k+b-c+d*e-g+h*i;
  return v[0]+v[3]+v[7]+v[11]; }
