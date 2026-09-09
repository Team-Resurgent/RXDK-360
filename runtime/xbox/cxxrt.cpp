//
// Minimal C++ runtime for the modern Xbox 360 runtime: operator new/delete over
// the console-pool malloc, and the Itanium-ABI helpers the compiler references
// for virtual dispatch, static locals and static-object destruction. Exceptions
// and RTTI are not here yet (titles build -fno-exceptions -fno-rtti); this is
// what non-throwing C++23 needs.
//
#include <stddef.h>

extern "C" void *malloc(size_t);
extern "C" void free(void *);

// ---- operator new / delete -------------------------------------------------

void *operator new(size_t n) { return malloc(n ? n : 1); }
void *operator new[](size_t n) { return malloc(n ? n : 1); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }

// ---- atexit / static-destructor table (file scope, internal linkage) -------

namespace {
struct AtExitEntry { void (*fn)(void *); void *arg; };
constexpr int kMaxAtExit = 256;
AtExitEntry g_atexit[kMaxAtExit];
int g_atexit_count = 0;
}  // namespace

// ---- Itanium C++ ABI helpers ----------------------------------------------

extern "C" {

// A call through a pure virtual slot is a bug; stop rather than run off.
void __cxa_pure_virtual(void) { for (;;) {} }
void __cxa_deleted_virtual(void) { for (;;) {} }

// Static-object destructor registration. Record the callbacks and run them in
// reverse order at title exit (crt_start.c calls __rxdk_run_atexit after main),
// so C++ static-object destructors and atexit handlers run like the console CRT.
// Registration is almost entirely pre-main static init (single-threaded); a few
// slots suffice for typical titles.
void *__dso_handle = 0;

int __cxa_atexit(void (*fn)(void *), void *arg, void *) {
    if (g_atexit_count >= kMaxAtExit) return -1;
    g_atexit[g_atexit_count].fn = fn;
    g_atexit[g_atexit_count].arg = arg;
    ++g_atexit_count;
    return 0;
}

int atexit(void (*fn)(void)) {
    return __cxa_atexit(reinterpret_cast<void (*)(void *)>(fn), 0, 0);
}

// Called by the CRT startup after main returns.
void __rxdk_run_atexit(void) {
    while (g_atexit_count > 0) {
        AtExitEntry e = g_atexit[--g_atexit_count];
        if (e.fn) e.fn(e.arg);
    }
}

// Thread-safe-static guards. Single-threaded here: the guard's first byte is the
// "initialised" flag; acquire tells the caller to run the init once.
int __cxa_guard_acquire(long long *g) { return !*reinterpret_cast<char *>(g); }
void __cxa_guard_release(long long *g) { *reinterpret_cast<char *>(g) = 1; }
void __cxa_guard_abort(long long *) {}

// The terminate/verbose-abort path in libc++abi tries to demangle the type name
// for a nicer message. We do not ship the (large) demangler; report "can't
// demangle" so it falls back to the raw mangled name. status: -2 = invalid name.
char *__cxa_demangle(const char *, char *, size_t *, int *status) {
    if (status) *status = -2;
    return 0;
}

}  // extern "C"
