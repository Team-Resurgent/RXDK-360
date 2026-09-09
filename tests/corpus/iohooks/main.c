#include "rt.h"

/* The libc I/O hooks (RXDK extra): write(1/2) routes through the output hook,
   read(0) through the stdin hook -- the paths printf/std::cout and getchar/
   std::cin sit on. Exercised here through write()/read() directly (printf over
   the bufio works standalone but has a combined-sample interaction, see
   docs/runtime.md). Declared locally to avoid <sys/types.h> in a test. */
typedef long     rxdk_ssize;
typedef unsigned rxdk_usize;
extern void rxdk_set_output_handler(rxdk_ssize (*)(int, const void *, rxdk_usize));
extern void rxdk_set_stdin_handler(rxdk_ssize (*)(void *, rxdk_usize));
extern rxdk_ssize write(int, const void *, rxdk_usize);
extern rxdk_ssize read(int, void *, rxdk_usize);

static char cap[128];
static int caplen;
static rxdk_ssize grab_output(int fd, const void *buf, rxdk_usize n) {
    const char *s = (const char *)buf;
    for (rxdk_usize i = 0; i < n && caplen < 127; i++) cap[caplen++] = s[i];
    cap[caplen] = 0;
    (void)fd;
    return (rxdk_ssize)n;
}
static rxdk_ssize feed_input(void *buf, rxdk_usize n) {
    static const char line[] = "Xbox360";
    static rxdk_usize pos;
    rxdk_usize i = 0;
    char *out = (char *)buf;
    while (i < n && line[pos]) out[i++] = line[pos++];
    return (rxdk_ssize)i;
}

void t_iohooks(void) {
    rxdk_set_output_handler(grab_output);
    write(1, "hooked output line\n", 19);
    rxdk_set_output_handler(0);
    DbgPrint("output hook captured: %s", cap);
    rxdk_set_stdin_handler(feed_input);
    char in[8];
    int n = 0;
    while (n < 7 && read(0, in + n, 1) == 1) n++;
    in[n] = 0;
    rxdk_set_stdin_handler(0);
    DbgPrint("stdin hook fed read: %s\n", in);
}
