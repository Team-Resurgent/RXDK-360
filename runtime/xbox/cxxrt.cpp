//
// Minimal C++ runtime for the modern Xbox 360 runtime: operator new/delete over
// the console-pool malloc, and the Itanium-ABI helpers the compiler references
// for virtual dispatch, static locals and static-object destruction. Exceptions
// and RTTI are not here yet (titles build -fno-exceptions -fno-rtti); this is
// what non-throwing C++23 needs.
//
#include <stddef.h>
#include <stdint.h>

extern "C" void *malloc(size_t);
extern "C" void free(void *);

// ---- operator new / delete -------------------------------------------------

void *operator new(size_t n) { return malloc(n ? n : 1); }
void *operator new[](size_t n) { return malloc(n ? n : 1); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }

// ---- over-aligned operator new / delete (C++17) ----------------------------
// The console-pool malloc is 16-aligned; an over-aligned request (e.g. the
// cache-line-aligned state <barrier>/<atomic> allocate) needs more. Over-
// allocate and align by hand, stashing the original pointer just below the
// aligned block so the matching delete can recover it. align_val_t is declared
// locally to avoid pulling <new> into this freestanding glue.
namespace std { enum class align_val_t : size_t {}; }

static void *rxdk_aligned_new(size_t n, size_t align) {
    if (align < sizeof(void *)) align = sizeof(void *);
    size_t total = (n ? n : 1) + align + sizeof(void *);
    void *raw = malloc(total);
    if (!raw) return 0;
    uintptr_t a = ((uintptr_t)raw + sizeof(void *) + (align - 1)) & ~(uintptr_t)(align - 1);
    ((void **)a)[-1] = raw;
    return (void *)a;
}
static void rxdk_aligned_delete(void *p) { if (p) free(((void **)p)[-1]); }

void *operator new(size_t n, std::align_val_t al) { return rxdk_aligned_new(n, (size_t)al); }
void *operator new[](size_t n, std::align_val_t al) { return rxdk_aligned_new(n, (size_t)al); }
void operator delete(void *p, std::align_val_t) noexcept { rxdk_aligned_delete(p); }
void operator delete[](void *p, std::align_val_t) noexcept { rxdk_aligned_delete(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { rxdk_aligned_delete(p); }
void operator delete[](void *p, size_t, std::align_val_t) noexcept { rxdk_aligned_delete(p); }

// ---- MSVC C++ ABI compat: operator new/delete + array ctor/dtor iterators ---
// Shipped cl.exe-built XDK libs reference the MSVC-mangled operator new/delete
// and the "eh vector" array-element iterators. Forward them to our Itanium ones
// so those libs link against our runtime (47 XDK libs need only this set -- see
// tools/lib_parity.py). Our operator new is malloc-backed and never throws, so
// the nothrow form is the same call. The asm() labels carry the MSVC-mangled
// names, which are not valid C++ identifiers. This is name aliasing only -- no
// MSVC C++ EH/RTTI/STL ABI is involved (those libs are otherwise C-clean).
extern "C++" {

// weak: a lib bundling its own STL (vcomp) provides strong operator new/new[];
// ours is the fallback for libs that don't.
__attribute__((weak)) void *rxdk_msvc_new(size_t n) asm("??2@YAPAXI@Z");
void *rxdk_msvc_new(size_t n) { return operator new(n); }

__attribute__((weak)) void *rxdk_msvc_new_nothrow(size_t n, const void *) asm("??2@YAPAXIABUnothrow_t@std@@@Z");
void *rxdk_msvc_new_nothrow(size_t n, const void *) { return operator new(n); }

__attribute__((weak)) void *rxdk_msvc_newa(size_t n) asm("??_U@YAPAXI@Z");
void *rxdk_msvc_newa(size_t n) { return operator new[](n); }

void rxdk_msvc_del(void *p) asm("??3@YAXPAX@Z");
void rxdk_msvc_del(void *p) { operator delete(p); }

void rxdk_msvc_dela(void *p) asm("??_V@YAXPAX@Z");
void rxdk_msvc_dela(void *p) { operator delete[](p); }

// eh vector ctor/dtor iterators: construct/destruct `count` elements of `size`
// bytes each. The MSVC versions also unwind already-built elements if a ctor
// throws; this glue is -fno-exceptions, so they are plain loops -- matching how
// the shipped (non-throwing) libs use them.
void rxdk_ehvec_ctor(void *p, size_t size, int count,
                     void (*ctor)(void *), void (*)(void *)) asm("??_L@YAXPAXIHP6AX0@Z1@Z");
void rxdk_ehvec_ctor(void *p, size_t size, int count,
                     void (*ctor)(void *), void (*)(void *)) {
    char *e = (char *)p;
    for (int i = 0; i < count; ++i, e += size) ctor(e);
}

void rxdk_ehvec_dtor(void *p, size_t size, int count,
                     void (*dtor)(void *)) asm("??_M@YAXPAXIHP6AX0@Z@Z");
void rxdk_ehvec_dtor(void *p, size_t size, int count,
                     void (*dtor)(void *)) {
    char *e = (char *)p + (size_t)count * size;
    for (int i = 0; i < count; ++i) { e -= size; dtor(e); }
}

// MSVC _set_new_handler(_PNH) where _PNH = int(*)(size_t). Our operator new is
// malloc-backed and never invokes a handler, so store and return the previous
// one for link + API completeness (it is never called).
typedef int (*rxdk_PNH)(size_t);
static rxdk_PNH g_ms_new_handler = 0;
rxdk_PNH rxdk_set_new_handler(rxdk_PNH h) asm("?_set_new_handler@@YAP6AHI@ZP6AHI@Z@Z");
rxdk_PNH rxdk_set_new_handler(rxdk_PNH h) { rxdk_PNH o = g_ms_new_handler; g_ms_new_handler = h; return o; }

}  // extern "C++"

// ---- MSVC C++ EH/RTTI stubs (for shipped libs that never throw) ------------
// A handful of cl.exe-built libs reference the MSVC C++ EH personality and RTTI
// vtable purely because /EHsc emits them for any function with a destructible
// local -- even ones that never actually throw (verified: d3dx9/nuispeech have
// 0 _CxxThrowException call sites). These stubs let those libs link and run
// correctly: the personality is only *invoked* during a real unwind, which those
// libs never originate. A lib that genuinely throws (xav/vcomp) will hit the
// loud abort until the real .pdata unwinder lands. This is deliberately separate
// from our Itanium EH -- no MSVC unwind is performed.
// __CxxFrameHandler / _CxxThrowException are now provided for real by the
// clean-room MSVC-EH runtime in runtime/xbox/msvc_eh.c (the "replacement obj"):
// the shipped MS libs reference them as externals and resolve to that object.
extern "C" {
extern void abort(void);
// The scalar-deleting destructor slot of the MS type_info vtable (never called
// unless RTTI is used at runtime, which these libs do not do).
static void rxdk_ti_dtor(void) {}
}  // extern "C"

// ??_7type_info@@6B@ -- the MS `type_info` vtable. Emitted/stored by libs with
// RTTI; a one-slot stub vtable satisfies the reference (no runtime RTTI here).
// The asm label carries the mangled name; `used` + external linkage keep the
// unreferenced-in-this-TU definition from being dropped.
extern void *const rxdk_type_info_vtable[] asm("??_7type_info@@6B@");
__attribute__((used)) void *const rxdk_type_info_vtable[] = { (void *)&rxdk_ti_dtor };

// ---- MS STL exception-glue stubs (xtms) ----
// std::_Xlength_error(const char*) throws length_error; the exception ctor path
// and stdext::exception dtor. No MSVC unwinder yet -> _Xlength_error fails loud;
// the rest are link-completeness data/dtor stubs (not called unless the lib
// actually raises, which these paths do not on success).
extern "C++" {
// public: virtual void * stdext::exception::`scalar deleting dtor'(unsigned)
__attribute__((weak))
void *rxdk_stdext_exc_dtor(void *self, unsigned flags) asm("??_Gexception@stdext@@UAAPAXI@Z");
void *rxdk_stdext_exc_dtor(void *self, unsigned flags) { (void)flags; return self; }
// void std::_Xlength_error(char const *)
__attribute__((weak))
void rxdk_Xlength_error(const char *) asm("?_Xlength_error@std@@YAXPBD@Z");
void rxdk_Xlength_error(const char *msg) { (void)msg; abort(); }
// void (*std::_Raise_handler)(stdext::exception const &) -- data pointer, null.
// weak: a lib bundling its own STL (vcomp) provides a strong copy that wins.
extern void *rxdk_raise_handler asm("?_Raise_handler@std@@3P6AXABVexception@stdext@@@ZA");
__attribute__((used, weak)) void *rxdk_raise_handler = 0;
// std::nothrow_t const std::nothrow -- empty object, 1 byte (weak, as above)
extern const char rxdk_std_nothrow asm("?nothrow@std@@3Unothrow_t@1@B");
__attribute__((used, weak)) const char rxdk_std_nothrow = 0;
// bool __uncaught_exception(void) -- MS CRT internal (imported by vcomp's bundled
// MS-STL); forward to our libc++abi uncaught-exception count.
extern "C" int __cxa_uncaught_exceptions(void);
bool rxdk_uncaught_exception(void) asm("?__uncaught_exception@@YA_NXZ");
bool rxdk_uncaught_exception(void) { return __cxa_uncaught_exceptions() > 0; }

// std::_Lockit / _Init_locks / _Mutex -- the MS STL locale/stream lock guards.
// Single-threaded locale here, so ctors/dtors/lock/unlock are no-ops (ctors
// return `this` per the MSVC ABI). WEAK: a lib bundling its own STL (vcomp)
// carries strong copies of these that override ours; ours are the fallback.
#define RXDK_STL_FALLBACK __attribute__((weak))
RXDK_STL_FALLBACK void *rxdk_Lockit_ctor(void *self, int) asm("??0_Lockit@std@@QAA@H@Z");
void *rxdk_Lockit_ctor(void *self, int kind) { (void)kind; return self; }
RXDK_STL_FALLBACK void  rxdk_Lockit_dtor(void *self) asm("??1_Lockit@std@@QAA@XZ");
void  rxdk_Lockit_dtor(void *self) { (void)self; }
RXDK_STL_FALLBACK void *rxdk_Initlocks_ctor(void *self) asm("??0_Init_locks@std@@QAA@XZ");
void *rxdk_Initlocks_ctor(void *self) { return self; }
RXDK_STL_FALLBACK void  rxdk_Initlocks_dtor(void *self) asm("??1_Init_locks@std@@QAA@XZ");
void  rxdk_Initlocks_dtor(void *self) { (void)self; }
RXDK_STL_FALLBACK void *rxdk_Mutex_ctor(void *self) asm("??0_Mutex@std@@QAA@XZ");
void *rxdk_Mutex_ctor(void *self) { return self; }
RXDK_STL_FALLBACK void  rxdk_Mutex_dtor(void *self) asm("??1_Mutex@std@@QAA@XZ");
void  rxdk_Mutex_dtor(void *self) { (void)self; }
RXDK_STL_FALLBACK void  rxdk_Mutex_lock(void *self) asm("?_Lock@_Mutex@std@@QAAXXZ");
void  rxdk_Mutex_lock(void *self) { (void)self; }
RXDK_STL_FALLBACK void  rxdk_Mutex_unlock(void *self) asm("?_Unlock@_Mutex@std@@QAAXXZ");
void  rxdk_Mutex_unlock(void *self) { (void)self; }
}

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
