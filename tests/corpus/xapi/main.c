#include "rt.h"

/* XAPI coverage: a batch of parameterless system queries. All are short-imports
   from xam.xex inside the XDK xapilib.lib -- resolved by the toolchain's
   import-by-real-module fix. Confirms the wider XAPI surface links and runs.
   Values are xenia's configured defaults (deterministic). */
extern unsigned XGetLanguage(void);
extern unsigned XGetGameRegion(void);
extern unsigned XGetAVPack(void);
extern unsigned XGetVideoStandard(void);
extern unsigned XGetAudioFlags(void);

void t_xapi(void) {
    DbgPrint("lang=%u region=0x%X avpack=%u vstd=%u aflags=0x%X\n",
             XGetLanguage(), XGetGameRegion(), XGetAVPack(),
             XGetVideoStandard(), XGetAudioFlags());
}
