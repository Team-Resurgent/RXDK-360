#include "rt.h"

/* XAM import: XGetLanguage is a short-import from xam.xex (ordinal 0x3CD) that
   lives inside the XDK xapilib.lib, not a static function. Exercises the
   toolchain resolving imports by their real source module (xam.xex), not by the
   containing .lib -- and confirms our XEX imports from xam like the official
   toolchain does. xenia's default user_language is 1 (English). */
extern unsigned XGetLanguage(void);

void t_xam(void) {
    DbgPrint("XGetLanguage=%u\n", XGetLanguage());
}
