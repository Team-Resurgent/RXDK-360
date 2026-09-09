/* <array>, <span>, container adaptors <stack>/<queue>/<priority_queue>. */
#include "rxdk_test.h"
#include <array>
#include <span>
#include <stack>
#include <queue>
#include <numeric>
#include <algorithm>
#include <vector>

int main(void) {
    std::array<int, 5> a = {5, 4, 3, 2, 1};
    CHECK_EQI((long)a.size(), 5, "array size");
    std::sort(a.begin(), a.end());
    CHECK(a[0] == 1 && a[4] == 5, "array sort");
    CHECK_EQI(std::accumulate(a.begin(), a.end(), 0), 15, "array accumulate");

    int raw[4] = {10, 20, 30, 40};
    std::span<int> sp(raw, 4);
    CHECK_EQI((long)sp.size(), 4, "span size");
    CHECK_EQI(sp[2], 30, "span index");
    CHECK_EQI(std::accumulate(sp.begin(), sp.end(), 0), 100, "span accumulate");
    auto sub = sp.subspan(1, 2);
    CHECK(sub.size() == 2 && sub[0] == 20, "span subspan");

    std::stack<int> st;
    st.push(1); st.push(2); st.push(3);
    CHECK(st.top() == 3, "stack top LIFO");
    st.pop();
    CHECK(st.top() == 2 && st.size() == 2, "stack pop");

    std::queue<int> q;
    q.push(1); q.push(2); q.push(3);
    CHECK(q.front() == 1 && q.back() == 3, "queue FIFO ends");
    q.pop();
    CHECK(q.front() == 2, "queue pop");

    std::priority_queue<int> pq;
    for (int x : {3, 1, 4, 1, 5, 9, 2}) pq.push(x);
    CHECK(pq.top() == 9, "priority_queue max on top");
    pq.pop();
    CHECK(pq.top() == 5, "priority_queue next max");

    CHECK_DONE("array_span");
    return 0;
}
