/* Sequence + associative + unordered containers. */
#include "rxdk_test.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <deque>
#include <list>
#include <string>

int main() {
    std::vector<int> v;
    for (int i = 0; i < 100; ++i) v.push_back(i);
    CHECK_EQI(v.size(), 100, "vector push_back 100");
    CHECK_EQI(v[50], 50, "vector index");
    CHECK_EQI(v.back(), 99, "vector back");
    v.pop_back();
    CHECK_EQI(v.size(), 99, "vector pop_back");
    v.insert(v.begin(), -1);
    CHECK_EQI(v.front(), -1, "vector insert front");

    std::map<std::string, int> m;
    m["a"] = 1; m["b"] = 2; m["c"] = 3;
    m["b"] += 40;
    CHECK_EQI(m.size(), 3, "map size");
    CHECK_EQI(m["b"], 42, "map update");
    CHECK(m.find("z") == m.end(), "map find missing");
    int order_ok = 1; std::string prev;
    for (auto &kv : m) { if (!prev.empty() && kv.first < prev) order_ok = 0; prev = kv.first; }
    CHECK(order_ok, "map iterates in sorted key order");

    std::unordered_map<int, int> um;
    for (int i = 0; i < 1000; ++i) um[i] = i * i;
    CHECK_EQI(um.size(), 1000, "unordered_map 1000 entries");
    CHECK_EQI(um[123], 123 * 123, "unordered_map lookup");
    CHECK(um.count(500) == 1 && um.count(5000) == 0, "unordered_map count");

    std::set<int> st;
    for (int x : {5, 3, 9, 3, 1, 9}) st.insert(x);
    CHECK_EQI(st.size(), 4, "set dedups");
    CHECK(*st.begin() == 1, "set min");
    CHECK(st.count(9) == 1, "set contains");

    std::deque<int> dq;
    dq.push_front(1); dq.push_back(2); dq.push_front(0);
    CHECK(dq.front() == 0 && dq.back() == 2, "deque push front/back");

    std::list<int> ls = {1, 2, 3};
    ls.reverse();
    CHECK(ls.front() == 3 && ls.back() == 1, "list reverse");

    CHECK_DONE("containers");
    return 0;
}
