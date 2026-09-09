#include "rt.h"
struct Base { virtual int val() const { return 1; } };
struct Derived : Base { int val() const override { return 42; } };
extern "C" void t_vcall(void) {
    Derived d;
    Base* b = &d;
    DbgPrint("vcall=%d\n", b->val());
}
