/* <future>: async / promise / packaged_task / shared_future, incl. exception
   propagation and timed waits. */
#include "rxdk_test.h"
#include <future>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <string>

int main() {
    // std::async(launch::async) runs on another thread; get() joins + returns.
    std::future<int> f = std::async(std::launch::async, [] { return 21 * 2; });
    CHECK_EQI(f.get(), 42, "async(launch::async) get()");

    // promise/future handoff across a thread.
    std::promise<int> p;
    std::future<int> pf = p.get_future();
    std::thread setter([&p] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        p.set_value(7);
    });
    CHECK_EQI(pf.get(), 7, "promise/future cross-thread value");
    setter.join();

    // packaged_task
    std::packaged_task<int(int, int)> task([](int a, int b) { return a + b; });
    std::future<int> tf = task.get_future();
    std::thread(std::move(task), 20, 22).detach();
    CHECK_EQI(tf.get(), 42, "packaged_task result");

    // shared_future: multiple getters see the same value.
    std::shared_future<int> sf = std::async(std::launch::async, [] { return 5; }).share();
    std::shared_future<int> sf2 = sf;
    CHECK(sf.get() == 5 && sf2.get() == 5, "shared_future multiple get");

    // exception propagates through the future.
    std::future<int> ef = std::async(std::launch::async, []() -> int {
        throw std::runtime_error("boom");
    });
    int threw = 0;
    try { ef.get(); } catch (const std::runtime_error &e) { threw = (std::string(e.what()) == "boom"); }
    CHECK(threw, "exception propagates through future::get");

    // wait_for reports timeout while the task is still sleeping, ready after.
    std::future<int> wf = std::async(std::launch::async, [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        return 99;
    });
    std::future_status st = wf.wait_for(std::chrono::milliseconds(1));
    CHECK(st == std::future_status::timeout, "wait_for reports timeout early");
    CHECK_EQI(wf.get(), 99, "future ready after wait");

    CHECK_DONE("future");
    return 0;
}
