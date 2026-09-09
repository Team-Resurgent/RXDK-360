/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * Tiny self-checking test framework for the RXDK-360 modern-runtime stdlib
 * suite. Each test is its own title; it runs a series of named CHECKs and each
 * emits one line the runner (tools/run_stdlib_tests.py) parses:
 *
 *     [T] PASS <description>
 *     [T] FAIL <description> (got .. want ..)
 *     [T] DONE <section> <pass>/<total>
 *
 * The DONE line lets the runner tell "some checks failed" from "the title
 * crashed before finishing". Output goes through DbgPrint, the console channel.
 * No golden files: the checks assert correctness themselves, so the runner
 * shows exactly what each section covers.
 */
#ifndef RXDK_TEST_H
#define RXDK_TEST_H

#ifdef __cplusplus
extern "C" {
#endif
int DbgPrint(const char *fmt, ...);
#ifdef __cplusplus
}
#endif

static int rxdk__pass = 0;
static int rxdk__total = 0;

/* boolean check */
#define CHECK(cond, desc)                              \
    do {                                               \
        ++rxdk__total;                                 \
        if (cond) {                                    \
            ++rxdk__pass;                              \
            DbgPrint("[T] PASS %s\n", (desc));         \
        } else {                                       \
            DbgPrint("[T] FAIL %s\n", (desc));         \
        }                                              \
    } while (0)

/* integer equality; prints got/want on failure (DbgPrint handles %ld, not %lld) */
#define CHECK_EQI(actual, expect, desc)                                    \
    do {                                                                   \
        long _a = (long)(actual), _e = (long)(expect);                     \
        ++rxdk__total;                                                     \
        if (_a == _e) {                                                    \
            ++rxdk__pass;                                                  \
            DbgPrint("[T] PASS %s\n", (desc));                             \
        } else {                                                           \
            DbgPrint("[T] FAIL %s (got %ld want %ld)\n", (desc), _a, _e);  \
        }                                                                  \
    } while (0)

/* C-string equality */
#define CHECK_STR(actual, expect, desc)                                    \
    do {                                                                   \
        const char *_a = (actual), *_e = (expect);                         \
        int _ok = _a && _e && rxdk__streq(_a, _e);                         \
        ++rxdk__total;                                                     \
        if (_ok) {                                                         \
            ++rxdk__pass;                                                  \
            DbgPrint("[T] PASS %s\n", (desc));                             \
        } else {                                                           \
            DbgPrint("[T] FAIL %s (got '%s' want '%s')\n", (desc),         \
                     _a ? _a : "(null)", _e ? _e : "(null)");              \
        }                                                                  \
    } while (0)

#define CHECK_DONE(section) \
    DbgPrint("[T] DONE %s %d/%d\n", (section), rxdk__pass, rxdk__total)

static int rxdk__streq(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

#endif
