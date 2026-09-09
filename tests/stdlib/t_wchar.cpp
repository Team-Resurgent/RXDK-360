/* wide characters: <cwchar> + std::wstring + wide streams. */
#include "rxdk_test.h"
#include <cwchar>
#include <string>
#include <sstream>
#include <iostream>

int main(void) {
    CHECK_EQI((long)std::wcslen(L"hello"), 5, "wcslen");

    std::wstring ws = L"wide";
    ws += L" string";
    CHECK_EQI((long)ws.size(), 11, "wstring append + size");
    CHECK(ws == L"wide string", "wstring compare");
    CHECK_EQI((long)ws.find(L"string"), 5, "wstring find");

    // wide string stream, captured
    {
        std::wostringstream os;
        os << L"n=" << 42 << L" x=" << 3.5;
        CHECK(os.str() == L"n=42 x=3.5", "wostringstream formatting");
    }
    {
        std::wistringstream is(L"100 2.5 word");
        int i = 0; double d = 0; std::wstring w;
        is >> i >> d >> w;
        CHECK_EQI(i, 100, "wistringstream >> int");
        CHECK(d == 2.5, "wistringstream >> double");
        CHECK(w == L"word", "wistringstream >> wstring");
    }

    // to_wstring / conversions
    CHECK(std::to_wstring(2026) == L"2026", "to_wstring");

    // wide <-> narrow via wcstombs / mbstowcs
    char nb[16]; std::wcstombs(nb, L"abc", sizeof nb);
    CHECK_STR(nb, "abc", "wcstombs wide->narrow");
    wchar_t wb[16]; std::mbstowcs(wb, "xyz", 16);
    CHECK(std::wstring(wb) == L"xyz", "mbstowcs narrow->wide");

    std::wcout << L"[wchar] wcout line\n";
    CHECK(std::wcout.good(), "wcout usable");

    CHECK_DONE("wchar");
    return 0;
}
