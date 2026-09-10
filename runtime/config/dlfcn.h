/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <dlfcn.h> -- picolibc ships none. A title is a single statically-linked image
 * with no runtime loader, so these are linkable stubs: dlopen() fails and
 * dlerror() reports why (see runtime/xbox/posix_unsupported.c). Symbols are
 * resolved at link time, not at run time.
 */
#ifndef _RXDK_DLFCN_H_
#define _RXDK_DLFCN_H_

#ifdef __cplusplus
extern "C" {
#endif

/* mode flags for dlopen() -- accepted but ignored (no loader) */
#define RTLD_LAZY   0x0001
#define RTLD_NOW    0x0002
#define RTLD_GLOBAL 0x0100
#define RTLD_LOCAL  0x0000

/* handle sentinels for dlsym() */
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT    ((void *)-1)

void  *dlopen(const char *file, int mode);
int    dlclose(void *handle);
void  *dlsym(void *handle, const char *name);
char  *dlerror(void);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_DLFCN_H_ */
