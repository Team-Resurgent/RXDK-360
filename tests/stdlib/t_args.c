/* main(argc, argv): the command-line parser (crt_start.c) + the plumbing.
   The parser is unit-tested with known inputs (deterministic); GetCommandLine on
   the console/emulator is usually empty, so main's own argc is just sanity. */
#include "rxdk_test.h"

extern int __rxdk_parse_cmdline(const char *cl, char *buf, int bufsz,
                                char **argv, int argmax);

int main(int argc, char **argv) {
    char b[256];
    char *av[16];
    int n;

    n = __rxdk_parse_cmdline("", b, sizeof b, av, 16);
    CHECK_EQI(n, 0, "empty command line -> argc 0");

    n = __rxdk_parse_cmdline(0, b, sizeof b, av, 16);
    CHECK_EQI(n, 0, "NULL command line -> argc 0");

    n = __rxdk_parse_cmdline("prog", b, sizeof b, av, 16);
    CHECK_EQI(n, 1, "single token -> argc 1");
    CHECK_STR(av[0], "prog", "argv[0]");
    CHECK(av[1] == 0, "argv[1] NULL terminator");

    n = __rxdk_parse_cmdline("game.xex -foo bar 123", b, sizeof b, av, 16);
    CHECK_EQI(n, 4, "four tokens");
    CHECK_STR(av[0], "game.xex", "argv[0] path");
    CHECK_STR(av[1], "-foo", "argv[1] flag");
    CHECK_STR(av[3], "123", "argv[3]");
    CHECK(av[4] == 0, "argv NULL-terminated");

    n = __rxdk_parse_cmdline("prog \"quoted arg\" x", b, sizeof b, av, 16);
    CHECK_EQI(n, 3, "quoted arg counts as one token");
    CHECK_STR(av[1], "quoted arg", "quoted token preserved with space");
    CHECK_STR(av[2], "x", "token after quoted");

    n = __rxdk_parse_cmdline("   spaced   out   ", b, sizeof b, av, 16);
    CHECK_EQI(n, 2, "leading/trailing/multiple spaces collapsed");
    CHECK_STR(av[0], "spaced", "first token");
    CHECK_STR(av[1], "out", "second token");

    /* plumbing: crt_start called main with a valid (argc, argv). */
    CHECK(argc >= 0, "main received argc");
    CHECK(argc == 0 || argv[argc] == 0, "main's argv is NULL-terminated");
    DbgPrint("[T] PASS main argc=%d (empty on a normal launch)\n", argc);
    ++rxdk__pass; ++rxdk__total;

    CHECK_DONE("args");
    return 0;
}
