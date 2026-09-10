/* vcomp (OpenMP runtime) linked against OUR libc/libcpp -- proves the shipped
   vcomp.lib runs on the RXDK runtime (no MS CRT). Calls the public omp_* API,
   which drives vcomp's internal state + locks + timing. */
extern int    DbgPrint(const char *, ...);
extern int    omp_get_num_procs(void);
extern int    omp_get_max_threads(void);
extern void   omp_set_num_threads(int);
extern int    omp_get_num_threads(void);
extern int    omp_get_thread_num(void);
extern int    omp_in_parallel(void);
extern double omp_get_wtime(void);
extern double omp_get_wtick(void);
extern void   omp_init_lock(void *);
extern void   omp_set_lock(void *);
extern void   omp_unset_lock(void *);
extern void   omp_destroy_lock(void *);

int main(void) {
    DbgPrint("[OMP] procs=%d max=%d\n", omp_get_num_procs(), omp_get_max_threads());

    omp_set_num_threads(4);
    DbgPrint("[OMP] after set_num_threads(4): max=%d in_parallel=%d tid=%d\n",
             omp_get_max_threads(), omp_in_parallel(), omp_get_thread_num());

    /* exercise a runtime lock through construct/lock/unlock/destroy */
    void *lock[16];   /* omp_lock_t is a small opaque handle; over-size for safety */
    omp_init_lock(lock);
    omp_set_lock(lock);
    DbgPrint("[OMP] lock acquired\n");
    omp_unset_lock(lock);
    omp_destroy_lock(lock);
    DbgPrint("[OMP] lock released+destroyed\n");

    double t0 = omp_get_wtime();
    volatile double acc = 0;
    for (int i = 0; i < 100000; ++i) acc += i;
    double t1 = omp_get_wtime();
    DbgPrint("[OMP] wtick=%g wtime delta>=0: %d (acc=%g)\n",
             omp_get_wtick(), (t1 - t0) >= 0.0, acc);

    DbgPrint("[OMP] ALLDONE\n");
    return 0;
}
