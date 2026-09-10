/* views::chunk + views::slide (C++23, P2442) -- implemented in the RXDK-360 llvm
   fork (upstream libc++ has not shipped them). */
#include "rxdk_test.h"
#include <ranges>
#include <vector>
#include <numeric>

int main() {
    std::vector<int> v{1, 2, 3, 4, 5, 6, 7};

    // chunk(3): [1,2,3] [4,5,6] [7]
    {
        int nchunks = 0, total = 0, last_sz = 0;
        for (auto&& c : v | std::views::chunk(3)) {
            ++nchunks;
            int sz = 0;
            for (int x : c) { total += x; ++sz; }
            last_sz = sz;
        }
        CHECK_EQI(nchunks, 3, "chunk(3) yields 3 chunks");
        CHECK_EQI(total, 28, "chunk(3) covers every element once");
        CHECK_EQI(last_sz, 1, "last chunk is the short remainder");
    }

    // chunk exact multiple: chunk(2) of 6 elems -> 3 full chunks
    {
        std::vector<int> w{10, 20, 30, 40, 50, 60};
        int nchunks = 0;
        for (auto&& c : w | std::views::chunk(2)) { (void)c; ++nchunks; }
        CHECK_EQI(nchunks, 3, "chunk(2) of 6 -> 3 full chunks");
    }

    // slide(3): windows [1,2,3][2,3,4][3,4,5][4,5,6][5,6,7]
    {
        int nwin = 0, sum_firsts = 0, last_win_sum = 0;
        for (auto&& win : v | std::views::slide(3)) {
            ++nwin;
            sum_firsts += *win.begin();
            last_win_sum = 0;
            for (int x : win) last_win_sum += x;
        }
        CHECK_EQI(nwin, 5, "slide(3) of 7 -> 5 windows");
        CHECK_EQI(sum_firsts, 1 + 2 + 3 + 4 + 5, "window starts are 1..5");
        CHECK_EQI(last_win_sum, 5 + 6 + 7, "last window is {5,6,7}");
    }

    // slide window size == range size -> exactly one window
    {
        std::vector<int> w{1, 2, 3};
        int nwin = 0;
        for (auto&& win : w | std::views::slide(3)) { (void)win; ++nwin; }
        CHECK_EQI(nwin, 1, "slide(n) over n elements -> 1 window");
    }

    CHECK_DONE("chunk_slide");
    return 0;
}
