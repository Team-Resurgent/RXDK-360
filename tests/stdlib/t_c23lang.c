/* C23 language features, asserted at runtime (not merely compiled): typeof,
   auto, constexpr, nullptr, _BitInt, bool/true/false keywords, binary literals,
   digit separators, enums with a fixed underlying type, and #embed. */
#include "rxdk_test.h"
#include <stddef.h>
#include <stdlib.h>

/* #embed pulls in tests/stdlib/t_c23lang.data (bytes 10,20,30,40). The runner
   puts tests/stdlib on the quote-include path, so this resolves in both the
   isolated and combined builds. */
static const unsigned char embedded[] = {
#embed "t_c23lang.data"
};

int main(void) {
    /* typeof */
    int a = 41;
    typeof(a) b = a + 1;
    CHECK_EQI(b, 42, "typeof(a) mirrors int");

    /* auto type inference */
    auto ai = 21 * 2;
    CHECK_EQI(ai, 42, "auto deduces int");
    auto ad = 1.5;
    CHECK_EQI((long)sizeof(ad), (long)sizeof(double), "auto deduces double");

    /* constexpr object usable as a constant expression */
    constexpr int N = 5;
    int arr[N];
    CHECK_EQI((long)(sizeof arr / sizeof arr[0]), 5, "constexpr as array bound");

    /* nullptr keyword */
    int *p = nullptr;
    CHECK(p == 0, "nullptr is the null pointer");
    typeof(nullptr) np = nullptr;
    CHECK(np == nullptr, "nullptr_t via typeof(nullptr)");

    /* _BitInt(N): exact-width integers */
    unsigned _BitInt(4) u4 = 15;
    u4 += 1;
    CHECK_EQI((long)u4, 0, "unsigned _BitInt(4) wraps 15->0");
    _BitInt(4) s4 = -8;
    CHECK_EQI((long)s4, -8, "signed _BitInt(4) holds -8");

    /* bool/true/false are keywords in C23 (no <stdbool.h>) */
    bool flag = true;
    CHECK(flag == true && !false, "bool/true/false keywords");

    /* binary literals + digit separators */
    CHECK_EQI(0b1010, 10, "binary literal 0b1010");
    CHECK_EQI(1'000'000, 1000000, "digit separators");

    /* enum with a fixed underlying type */
    enum Small : unsigned char { S_A = 1, S_B = 2 };
    CHECK_EQI((long)sizeof(enum Small), 1, "enum : unsigned char is 1 byte");
    CHECK_EQI((long)S_B, 2, "fixed-underlying enum value");

    /* #embed */
    CHECK_EQI((long)sizeof(embedded), 4, "#embed byte count");
    CHECK(embedded[0] == 10 && embedded[3] == 40, "#embed file contents");

    /* C23 sized deallocation (7.24.3.3/4): both forward to free() here. */
    void *m = malloc(64);
    free_sized(m, 64);
    void *am = aligned_alloc(32, 128);
    free_aligned_sized(am, 32, 128);
    CHECK(m != nullptr && am != nullptr, "free_sized/free_aligned_sized link and run");

    CHECK_DONE("c23lang");
    return 0;
}
