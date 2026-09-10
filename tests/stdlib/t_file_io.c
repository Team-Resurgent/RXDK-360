/* C standard file I/O over the kernel backend (cache: = writable utility drive). */
#include "rxdk_test.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    const char *path = "cache:/t_file_io.txt";
    const char *msg = "hello rxdk360\n";

    FILE *f = fopen(path, "wb");
    CHECK(f != NULL, "fopen for write");
    if (f) {
        size_t w = fwrite(msg, 1, strlen(msg), f);
        CHECK_EQI(w, (long)strlen(msg), "fwrite byte count");
        CHECK(fprintf(f, "n=%d\n", 42) > 0, "fprintf to file");
        CHECK_EQI(fclose(f), 0, "fclose");
    }

    f = fopen(path, "rb");
    CHECK(f != NULL, "reopen for read");
    if (f) {
        char buf[128];
        memset(buf, 0, sizeof buf);
        size_t r = fread(buf, 1, sizeof buf - 1, f);
        CHECK(r > strlen(msg), "fread returned data");
        CHECK(strncmp(buf, msg, strlen(msg)) == 0, "read-back matches written prefix");

        CHECK_EQI(fseek(f, 0, SEEK_SET), 0, "fseek to start");
        CHECK_EQI(ftell(f), 0, "ftell at start");
        CHECK_EQI(fseek(f, 0, SEEK_END), 0, "fseek to end");
        CHECK(ftell(f) == (long)r, "ftell at end == bytes read");

        int c;
        CHECK_EQI(fseek(f, 0, SEEK_SET), 0, "rewind");
        c = fgetc(f);
        CHECK_EQI(c, 'h', "fgetc first char");
        fclose(f);
    }

    /* stat()/fstat() must report the real byte length (a live handle query on
       xenia, not the stale 0 the cached directory entry can carry). */
    off_t total = (off_t)(strlen(msg) + strlen("n=42\n"));
    struct stat sb;
    CHECK_EQI(stat(path, &sb), 0, "stat succeeds");
    CHECK(sb.st_size == total, "stat st_size == bytes written (non-zero)");
    CHECK(S_ISREG(sb.st_mode), "stat st_mode is a regular file");

    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0, "open for fstat");
    if (fd >= 0) {
        struct stat fb;
        CHECK_EQI(fstat(fd, &fb), 0, "fstat succeeds");
        CHECK(fb.st_size == total, "fstat st_size == bytes written (non-zero)");
        close(fd);
    }

    CHECK_EQI(remove(path), 0, "remove file");
    f = fopen(path, "rb");
    CHECK(f == NULL, "removed file no longer opens");
    if (f) fclose(f);

    CHECK_DONE("file_io");
    return 0;
}
