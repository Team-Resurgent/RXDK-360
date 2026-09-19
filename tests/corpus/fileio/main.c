#include "rt.h"

/* Regression for issue #4: calling fopen() from title code used to wedge the
   console -- a bare relative path with no resolvable device blocked forever
   inside the file layer, with no error and no exception, and XBDM died. The fix
   (pathres.c defaults the cwd to "game:\" and resolves every relative path
   before the kernel call; fileio.c's nt_open checks the NTSTATUS and returns)
   means fopen now either succeeds or fails with NULL -- it never hangs.

   This asserts only environment-independent invariants, so the one expected.txt
   passes on BOTH xenia and real hardware (where game: writability differs):
     - a bare relative-path open RETURNS at all (a hang would cost the corpus
       sentinel and fail the whole run -- this line is the real repro of #4),
     - opening a missing file for read fails cleanly with NULL,
     - an unresolvable device prefix fails cleanly with NULL, never blocking. */

extern void *fopen(const char *path, const char *mode);
extern int fclose(void *f);

void t_fileio(void) {
    /* the exact shape that used to wedge (issue #4): a bare relative path. Its
       success is environment-dependent (game: may be read-only), so we assert
       only that the call RETURNED -- reaching the next line proves no hang. */
    void *f = fopen("rxdk_fileio_probe.txt", "w");
    if (f) fclose(f);
    DbgPrint("relative_open_returned\n");

    /* reading an absent file exercises resolve + open + clean failure */
    void *miss = fopen("rxdk_absent_9e3f.dat", "r");
    DbgPrint("missing_read_null=%d\n", miss == 0);

    /* unresolvable device prefix: fail cleanly with NULL, never block */
    void *bad = fopen("nodev:\\nope.txt", "r");
    DbgPrint("bad_device_null=%d\n", bad == 0);
}
