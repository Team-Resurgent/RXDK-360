/* libc/posix functions newly built in: the POSIX regex engine and strfmon are
   real and exercised here; the exec* family and the passwd/group DB are real
   front-ends whose backends the console cannot provide (no second process, no
   /etc/passwd), so they must fail cleanly rather than crash. */
#include "rxdk_test.h"
#include <sys/types.h>
#include <regex.h>
#include <monetary.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

int main(void) {
    /* ---- POSIX regex (real Spencer engine) ---- */
    regex_t re;
    CHECK_EQI(regcomp(&re, "^a.c$", REG_EXTENDED), 0, "regcomp ^a.c$");
    CHECK_EQI(regexec(&re, "abc", 0, NULL, 0), 0, "regex match 'abc'");
    CHECK_EQI(regexec(&re, "axc", 0, NULL, 0), 0, "regex match 'axc'");
    CHECK_EQI(regexec(&re, "abbc", 0, NULL, 0), REG_NOMATCH, "regex no-match 'abbc'");
    regfree(&re);

    regex_t re2;
    regmatch_t m[3];
    CHECK_EQI(regcomp(&re2, "([0-9]+)-([0-9]+)", REG_EXTENDED), 0, "regcomp capture groups");
    CHECK_EQI(regexec(&re2, "foo 12-345 bar", 3, m, 0), 0, "regex groups match");
    CHECK(m[1].rm_so >= 0 && (m[1].rm_eo - m[1].rm_so) == 2, "group 1 spans '12'");
    CHECK((m[2].rm_eo - m[2].rm_so) == 3, "group 2 spans '345'");
    regfree(&re2);

    /* a malformed pattern must be rejected cleanly, not crash (regression: the
       fork's seterr frees the cset, so p_bracket must bail before finalizing it) */
    regex_t bad;
    int rc = regcomp(&bad, "[unclosed", REG_EXTENDED);
    CHECK(rc != 0, "regcomp rejects an unterminated bracket");
    char eb[64];
    CHECK(regerror(rc, &bad, eb, sizeof eb) > 0, "regerror returns a message");

    /* ---- strfmon (real monetary formatter) ---- */
    char mb[64];
    ssize_t mn = strfmon(mb, sizeof mb, "%n", 1234.56);
    CHECK(mn > 0, "strfmon returns a length");
    CHECK(strstr(mb, "1234") != NULL, "strfmon formatted the amount");

    /* ---- exec family: real front-ends, no process to exec into ---- */
    char *av[] = { (char *)"x", NULL };
    errno = 0;
    CHECK(execv("game:\\x.xex", av) == -1, "execv -> -1 (no process model)");
    errno = 0;
    CHECK(execlp("x", "x", (char *)NULL) == -1, "execlp -> -1");

    /* ---- passwd/group DB: real, file-backed, but the console has no files ---- */
    errno = 0;
    CHECK(getpwuid(0) == NULL, "getpwuid -> NULL (no passwd db)");
    CHECK(getpwnam("root") == NULL, "getpwnam -> NULL");
    CHECK(getgrgid(0) == NULL, "getgrgid -> NULL (no group db)");

    /* ---- wait: real wrapper over waitpid, no children ---- */
    int st;
    errno = 0;
    CHECK(wait(&st) == -1, "wait -> -1 (no children)");

    CHECK_DONE("posix2");
    return 0;
}
