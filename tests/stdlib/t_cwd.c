/* Current-working-directory and relative-path support: the 360 kernel needs
   fully-qualified paths, so libc keeps a cwd (default = the launch drive) and
   resolves relative paths, '.', and '..' against it before every kernel call. */
#include "rxdk_test.h"
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>

int main(void) {
    char buf[256];

    /* default cwd is the drive the title launched from */
    CHECK(getcwd(buf, sizeof buf) != NULL, "getcwd non-null");
    CHECK_STR(buf, "game:\\", "default cwd == launch drive (game:\\)");

    /* chdir to an absolute writable directory */
    mkdir("cache:/cwdtest", 0777);
    CHECK_EQI(chdir("cache:/cwdtest"), 0, "chdir absolute");
    CHECK(getcwd(buf, sizeof buf) && strcmp(buf, "cache:\\cwdtest") == 0,
          "getcwd reflects chdir");

    /* a relative open() resolves against the cwd */
    int fd = open("rel.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    CHECK(fd >= 0, "open relative for write");
    if (fd >= 0) { write(fd, "hi", 2); close(fd); }
    struct stat st;
    CHECK_EQI(stat("cache:/cwdtest/rel.txt", &st), 0,
              "relative file created at the resolved absolute path");
    CHECK(st.st_size == 2, "relative file has the written bytes");
    char rb[8] = {0};
    fd = open("rel.txt", O_RDONLY);
    CHECK(fd >= 0, "open relative for read");
    if (fd >= 0) {
        CHECK(read(fd, rb, sizeof rb) == 2 && rb[0] == 'h', "relative read-back");
        close(fd);
    }

    /* relative chdir into a subdir, then chdir("..") */
    mkdir("cache:/cwdtest/sub", 0777);
    CHECK_EQI(chdir("sub"), 0, "chdir relative into subdir");
    CHECK(getcwd(buf, sizeof buf) && strcmp(buf, "cache:\\cwdtest\\sub") == 0,
          "cwd after relative chdir");
    CHECK_EQI(chdir(".."), 0, "chdir ..");
    CHECK(getcwd(buf, sizeof buf) && strcmp(buf, "cache:\\cwdtest") == 0,
          "cwd after .. pops one component");

    /* realpath resolves relative + normalizes '.' and '..' */
    char rp[256];
    CHECK(realpath("./rel.txt", rp) && strcmp(rp, "cache:\\cwdtest\\rel.txt") == 0,
          "realpath: './' collapsed against cwd");
    CHECK(realpath("sub/../rel.txt", rp) && strcmp(rp, "cache:\\cwdtest\\rel.txt") == 0,
          "realpath: '..' collapsed");

    /* opendir on the cwd via '.' */
    DIR *d = opendir(".");
    CHECK(d != NULL, "opendir '.' (the cwd)");
    if (d) closedir(d);

    /* chdir to a missing directory fails */
    CHECK(chdir("cache:/no_such_dir_xyz") == -1, "chdir missing dir -> -1");

    chdir("game:\\");   /* restore for any later section */
    CHECK_DONE("cwd");
    return 0;
}
