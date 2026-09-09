#include "rt.h"

// Real kernel threads (C11 <threads.h> over ExCreateThread) + per-thread
// exception storage. Four workers run concurrently, each throwing and catching
// its OWN exception; if the libc++abi exception state were shared rather than
// thread-local, the concurrently-in-flight exceptions would clobber each other
// and the caught ids would come out wrong. Workers return their result and the
// main thread prints after joining, so the output is deterministic.
extern "C" {
typedef int (*thrd_start_t)(void *);
int thrd_create(void **thr, thrd_start_t func, void *arg);
int thrd_join(void *thr, int *res);
}

namespace {
struct WorkerEx { int id; };

int worker(void *arg) {
    int id = (int)(long)arg;
    int caught = -1;
    // Throw/catch many times so the four threads unwind concurrently -- stresses
    // the per-thread exception storage and libunwind's (now-locked) FDE cache.
    for (int n = 0; n < 64; ++n) {
        try {
            throw WorkerEx{id * 100 + 7};
        } catch (WorkerEx &e) {
            caught = e.id;
        }
    }
    return caught;
}
}  // namespace

extern "C" void t_threads(void) {
    const int N = 4;
    void *th[N];
    int res[N];
    for (int i = 0; i < N; ++i)
        thrd_create(&th[i], worker, (void *)(long)i);
    for (int i = 0; i < N; ++i) {
        res[i] = -1;
        thrd_join(th[i], &res[i]);
    }
    for (int i = 0; i < N; ++i)
        DbgPrint("  thread %d caught %d\n", i, res[i]);
    DbgPrint("  threads ok\n");
}
