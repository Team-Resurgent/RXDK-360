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

// ---- Itanium C++ ABI helpers ----------------------------------------------

extern "C" {

// A call through a pure virtual slot is a bug; stop rather than run off.
void __cxa_pure_virtual(void) { for (;;) {} }
void __cxa_deleted_virtual(void) { for (;;) {} }

// Static-object destructor registration. A title powers off rather than
// returning from main, so destructors never run -- record nothing.
void *__dso_handle = 0;
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
int atexit(void (*)(void)) { return 0; }

// Thread-safe-static guards. Single-threaded here: the guard's first byte is the
// "initialised" flag; acquire tells the caller to run the init once.
int __cxa_guard_acquire(long long *g) { return !*reinterpret_cast<char *>(g); }
void __cxa_guard_release(long long *g) { *reinterpret_cast<char *>(g) = 1; }
void __cxa_guard_abort(long long *) {}

}  // extern "C"
