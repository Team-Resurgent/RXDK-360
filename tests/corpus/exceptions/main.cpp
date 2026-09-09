#include "rt.h"

// C++ exception runtime (DWARF/Itanium EH via libunwind + libc++abi). Exercises
// throw/catch by reference, destructor execution during unwinding, rethrow
// through a nested frame, catch-by-base (RTTI type matching), and catch-all.
namespace {
struct Boom { int code; };
struct Guard { const char *n; ~Guard() { DbgPrint("  ~Guard(%s)\n", n); } };
struct Base { virtual ~Base() {} };
struct Derived : Base {};

void rethrower() {
    try {
        throw Boom{7};
    } catch (Boom &b) {
        DbgPrint("  inner caught code=%d, rethrow\n", b.code);
        throw;  // rethrow the in-flight exception
    }
}
}  // namespace

extern "C" void t_exceptions(void) {
    try {
        Guard g{"outer"};
        DbgPrint("  throwing\n");
        throw Boom{42};
        DbgPrint("  unreachable\n");
    } catch (Boom &b) {
        DbgPrint("  caught code=%d\n", b.code);
    }

    try {
        rethrower();
    } catch (Boom &b) {
        DbgPrint("  outer caught rethrown code=%d\n", b.code);
    }

    try {
        throw Derived{};
    } catch (Base &) {
        DbgPrint("  caught Derived as Base&\n");
    }

    try {
        throw 123;
    } catch (...) {
        DbgPrint("  caught via ...\n");
    }

    DbgPrint("  exceptions ok\n");
}
