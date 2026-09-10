/* C++20/23 concurrency built on atomic wait: jthread + stop_token, latch,
   barrier, counting_semaphore, atomic_ref, and atomic wait/notify. These lean on
   libc++'s atomic-wait backend, so this section is the real test that it works on
   bare-metal 360 (not just that it compiles). */
#include "rxdk_test.h"
#include <thread>
#include <stop_token>
#include <latch>
#include <barrier>
#include <semaphore>
#include <atomic>
#include <chrono>
#include <vector>

int main() {
    // jthread joins in its destructor and passes a stop_token.
    {
        std::atomic<int> ran{0};
        {
            std::jthread jt([&ran](std::stop_token st) {
                while (!st.stop_requested())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ran = 1;
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            jt.request_stop();
        } // ~jthread joins
        CHECK_EQI(ran.load(), 1, "jthread stop_token + auto-join");
    }

    // latch: N workers count down, main waits for zero.
    {
        const int N = 4;
        std::latch done(N);
        std::atomic<int> sum{0};
        std::vector<std::thread> ts;
        for (int i = 1; i <= N; ++i)
            ts.emplace_back([&, i] { sum += i; done.count_down(); });
        done.wait();
        CHECK_EQI(sum.load(), 10, "latch: all workers counted down before wait returned");
        for (auto &t : ts) t.join();
    }

    // barrier: two threads rendezvous twice.
    {
        std::atomic<int> phase{0};
        std::barrier sync(2);
        auto worker = [&] { sync.arrive_and_wait(); phase.fetch_add(1); sync.arrive_and_wait(); };
        std::thread a(worker), b(worker);
        a.join(); b.join();
        CHECK_EQI(phase.load(), 2, "barrier rendezvous (both passed the phase)");
    }

    // counting_semaphore as a 2-permit gate.
    {
        std::counting_semaphore<8> sem(0);
        std::atomic<int> got{0};
        std::thread t([&] { sem.acquire(); got = 1; });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        int before = got.load();
        sem.release();
        t.join();
        CHECK(before == 0 && got.load() == 1, "counting_semaphore blocks then releases");
    }

    // atomic_ref: concurrent fetch_add on a plain int.
    {
        int counter = 0;
        std::atomic_ref<int> ref(counter);
        std::vector<std::thread> ts;
        for (int i = 0; i < 4; ++i)
            ts.emplace_back([&] { for (int k = 0; k < 1000; ++k) ref.fetch_add(1); });
        for (auto &t : ts) t.join();
        CHECK_EQI(counter, 4000, "atomic_ref concurrent fetch_add");
    }

    // atomic wait/notify: waiter blocks until the value changes.
    {
        std::atomic<int> a{0};
        std::atomic<int> woke{0};
        std::thread w([&] { a.wait(0); woke = 1; });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        int before = woke.load();
        a.store(1);
        a.notify_one();
        w.join();
        CHECK(before == 0 && woke.load() == 1, "atomic wait/notify");
    }

    CHECK_DONE("concurrency");
    return 0;
}
