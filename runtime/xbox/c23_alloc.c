/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * C23 sized deallocation (7.24.3.3/4). free_sized/free_aligned_sized let a caller
 * hand the allocator the size (and alignment) it originally requested, so an
 * allocator that keys its bookkeeping on size can skip the lookup. picolibc's
 * allocator does not use the hint, so both simply forward to free() -- which is a
 * conforming implementation (the size/alignment are optimisations, not required
 * to be used). The declarations are added to <stdlib.h> by
 * patches/picolibc/0001-c23-free-sized.patch.
 */
#include <stdlib.h>

void free_sized(void *ptr, size_t size)
{
    (void)size;
    free(ptr);
}

void free_aligned_sized(void *ptr, size_t alignment, size_t size)
{
    (void)alignment;
    (void)size;
    free(ptr);
}
