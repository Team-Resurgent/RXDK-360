/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <crypt.h> -- picolibc ships none. The one-way password hash crypt(3) needs a
 * DES/MD5/bcrypt back end the runtime does not carry; provided as a clean-
 * failing stub (returns NULL / ENOSYS) so portable code links -- see
 * runtime/xbox/posix_unsupported.c.
 */
#ifndef _RXDK_CRYPT_H_
#define _RXDK_CRYPT_H_

#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

struct crypt_data {
    char keysched[16 * 8];
    char sb0[32768];
    char sb1[32768];
    char sb2[32768];
    char sb3[32768];
    char crypt_3_buf[14];
    char current_salt[2];
    long current_saltbits;
    int  direction;
    int  initialized;
};

char *crypt(const char *key, const char *salt);
char *crypt_r(const char *key, const char *salt, struct crypt_data *data);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_CRYPT_H_ */
