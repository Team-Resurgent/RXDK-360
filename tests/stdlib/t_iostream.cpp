/* <sstream>/<iostream> formatting -- captured via ostringstream so results are
   deterministic and checkable (cout/cerr also exercised, output side only). */
#include "rxdk_test.h"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <string>

int main() {
    {
        std::ostringstream os;
        os << "n=" << 42 << " f=" << 3.5 << " s=" << std::string("hi");
        CHECK_STR(os.str().c_str(), "n=42 f=3.5 s=hi", "ostringstream mixed insert");
    }
    {
        std::ostringstream os;
        os << std::hex << 255 << " " << std::oct << 8 << std::dec << " " << 10;
        CHECK_STR(os.str().c_str(), "ff 10 10", "hex/oct/dec manipulators");
    }
    {
        std::ostringstream os;
        os << std::setw(5) << std::setfill('0') << 42;
        CHECK_STR(os.str().c_str(), "00042", "setw/setfill");
    }
    {
        std::istringstream is("123 45.5 word");
        int i = 0; double d = 0; std::string w;
        is >> i >> d >> w;
        CHECK_EQI(i, 123, "istringstream >> int");
        CHECK(d == 45.5, "istringstream >> double");
        CHECK_STR(w.c_str(), "word", "istringstream >> string");
        CHECK(!is.fail(), "stream state good");
    }
    {
        std::istringstream is("10 20 30");
        int sum = 0, x;
        while (is >> x) sum += x;
        CHECK_EQI(sum, 60, "istringstream loop to EOF");
        CHECK(is.eof(), "eof set after loop");
    }
    /* cout/cerr: output-only smoke (not captured), proves they don't fault */
    std::cout << "[iostream] cout line\n";
    std::cerr << "[iostream] cerr line\n";
    CHECK(std::cout.good(), "cout still good after write");

    CHECK_DONE("iostream");
    return 0;
}
