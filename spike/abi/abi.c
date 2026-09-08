struct S8  { int a, b; };
struct S20 { int a, b, c, d, e; };

int  many(int a,int b,int c,int d,int e,int f,int g,int h,int i,int j,int k,int l);
int  mixed(int a,float b,int c,double d,int e,float f);
int  sbv(struct S8 s, int x);
int  sbv20(struct S20 s, int x);
int  va(const char* fmt, ...);
long long ll(long long a, int b, long long c);

int call_many(void)  { return many(1,2,3,4,5,6,7,8,9,10,11,12); }
int call_mixed(void) { return mixed(1,2.0f,3,4.0,5,6.0f); }
int call_sbv(void)   { struct S8 s = {1,2}; return sbv(s, 9); }
int call_sbv20(void) { struct S20 s = {1,2,3,4,5}; return sbv20(s, 9); }
int call_va(void)    { return va("%d %d %f", 1, 2, 3.0); }
long long call_ll(void) { return ll(1,2,3); }

int def_many(int a,int b,int c,int d,int e,int f,int g,int h,int i,int j,int k,int l)
{ return a+b+c+d+e+f+g+h+i+j+k+l; }
