#include "rt.h"

// A global object with a non-trivial constructor: clang registers the ctor in
// .init_array, which start.c runs pre-main (like xapilib's startup). If pre-main
// init did not run, g.v would still be 0.
struct Global { int v; Global() { v = 1234; } };
static Global g;

extern "C" void t_cppinit(void) {
    DbgPrint("ctor: g.v=%d\n", g.v);
    int* p = new int(77);
    DbgPrint("new: *p=%d\n", *p);
    delete p;
    DbgPrint("delete ok\n");
    // a function-static local exercises the __cxa_guard path
    static Global once;
    DbgPrint("static-local: v=%d\n", once.v);
}
