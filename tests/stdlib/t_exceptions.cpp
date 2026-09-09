/* C++ exceptions: throw/catch, std types, rethrow, nested, catch-by-base. */
#include "rxdk_test.h"
#include <stdexcept>
#include <string>
#include <exception>

static int throw_if(int x) { if (x) throw std::runtime_error("boom"); return 7; }

struct Base { virtual ~Base() {} };
struct Derived : Base {};

int main() {
    int caught = 0;
    try { throw_if(1); } catch (const std::runtime_error &e) {
        caught = 1;
        CHECK_STR(e.what(), "boom", "runtime_error::what()");
    }
    CHECK(caught, "caught thrown exception");

    CHECK_EQI(throw_if(0), 7, "no throw returns normally");

    try {
        throw std::out_of_range("oor");
    } catch (const std::exception &e) {
        CHECK_STR(e.what(), "oor", "catch out_of_range as std::exception (base)");
    }

    // rethrow
    int rethrown = 0;
    try {
        try { throw std::logic_error("inner"); }
        catch (...) { throw; }
    } catch (const std::logic_error &e) {
        rethrown = 1;
        CHECK_STR(e.what(), "inner", "rethrow preserves object");
    }
    CHECK(rethrown, "rethrow reached outer handler");

    // polymorphic catch
    int poly = 0;
    try { throw Derived(); } catch (const Base &) { poly = 1; }
    CHECK(poly, "catch derived by base reference");

    // stack unwinding runs destructors
    static int dtor_ran = 0;
    struct Guard { ~Guard() { dtor_ran = 1; } };
    try { Guard g; throw 1; } catch (int) {}
    CHECK(dtor_ran, "destructor ran during unwinding");

    // exception_ptr
    std::exception_ptr ep;
    try { throw std::runtime_error("stored"); } catch (...) { ep = std::current_exception(); }
    int restored = 0;
    try { std::rethrow_exception(ep); } catch (const std::runtime_error &e) {
        restored = (std::string(e.what()) == "stored");
    }
    CHECK(restored, "exception_ptr store + rethrow_exception");

    CHECK_DONE("exceptions");
    return 0;
}
