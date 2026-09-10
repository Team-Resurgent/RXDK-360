/* <spanstream> (C++23) -- fixed-buffer streams over a std::span, no allocation.
   Implemented in the RXDK-360 llvm fork (upstream libc++ has not shipped it). */
#include "rxdk_test.h"
#include <spanstream>
#include <string>
#include <span>

int main() {
    // ospanstream: format into a caller-provided buffer, no heap.
    char buf[64] = {};
    {
        std::ospanstream os{std::span<char>(buf)};
        os << "n=" << 42 << " x=" << 3.5;
        auto sp = os.span();                 // written portion only
        std::string out(sp.data(), sp.size());
        CHECK(out == "n=42 x=3.5", "ospanstream formats into the span");
        CHECK_EQI((long)sp.size(), 10, "span() is the written length");
    }

    // ispanstream: parse formatted values out of a span.
    {
        char in[] = "100 2.5 word";
        std::ispanstream is{std::span<char>(in, 12)};
        int i = 0; double d = 0; std::string w;
        is >> i >> d >> w;
        CHECK_EQI(i, 100, "ispanstream >> int");
        CHECK(d == 2.5, "ispanstream >> double");
        CHECK(w == "word", "ispanstream >> string");
    }

    // fixed capacity: writing past the end sets failbit (no growth).
    {
        char small[4] = {};
        std::ospanstream os{std::span<char>(small)};
        os << "abcdefgh";              // longer than 4
        CHECK(os.fail(), "overflow past the fixed span sets failbit");
    }

    // spanstream (in+out) round-trip through one buffer.
    {
        char rt[32] = {};
        std::spanstream ss{std::span<char>(rt)};
        ss << 7 << ' ' << 8;
        int a = 0, b = 0;
        ss.seekg(0);
        ss >> a >> b;
        CHECK(a == 7 && b == 8, "spanstream write then seek+read round-trips");
    }

    CHECK_DONE("spanstream");
    return 0;
}
