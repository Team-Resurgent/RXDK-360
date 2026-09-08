typedef float V __attribute__((vector_size(16)));
extern void sinkv(V);
int def_mixv(int a, V b, float c, V d, int e) { sinkv(b); sinkv(d); return a + e; }
