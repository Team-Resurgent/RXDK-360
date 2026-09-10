/* <print> (C++23): std::print / std::println. This libc++ ships only the FILE*
   forms (to stdout), so we capture stdout through the RXDK output hook and assert
   what std::print actually wrote. (CHECK uses DbgPrint, a separate kernel path,
   so it still works while the hook is installed.) */
#include "rxdk_test.h"
#include <print>
#include <cstdio>
#include <cstring>
#include <sys/types.h>   /* ssize_t */

extern "C" void rxdk_set_output_handler(ssize_t (*)(int, const void *, size_t));

static char cap[256];
static size_t caplen;
static ssize_t grab(int fd, const void *buf, size_t n) {
    (void)fd;
    for (size_t i = 0; i < n && caplen < sizeof(cap) - 1; ++i)
        cap[caplen++] = ((const char *)buf)[i];
    cap[caplen] = '\0';
    return (ssize_t)n;
}
static void reset() { caplen = 0; cap[0] = '\0'; }

int main() {
    rxdk_set_output_handler(grab);

    reset();
    std::print("{} {} {}", 1, "two", 3.5);
    std::fflush(stdout);
    CHECK_STR(cap, "1 two 3.5", "print formats to stdout");

    reset();
    std::println("line {}", 42);
    std::fflush(stdout);
    CHECK_STR(cap, "line 42\n", "println appends newline");

    reset();
    std::print("{:*>6}", 42);
    std::fflush(stdout);
    CHECK_STR(cap, "****42", "print honours format spec");

    rxdk_set_output_handler(0);
    CHECK_DONE("print");
    return 0;
}
