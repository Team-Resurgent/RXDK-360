/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * Standalone "smoke" probe for the system / online libs that cannot fully
 * function under xenia (no console / no Xbox Live). Each probe is bracketed
 * with a "before"/"after" DbgPrint so that if a probe faults (runs real guest
 * lib code that dereferences uninitialised state), the last unmatched "before"
 * names the culprit lib -- the same de-risking pattern used for xuitest.cpp.
 *
 * Survivors (probes that return cleanly) graduate into dash.cpp's SYSTEM
 * section; anything that faults stays documented here as link-only. All four
 * probed here survived; they are now in the dashboard's SYSTEM section.
 *
 * Build (no shaders/media, so no build_*.py needed):
 *   clang++ --target=powerpc-unknown-xbox360 -std=c++11 -O2 -fms-extensions \
 *     -fms-compatibility -fdeclspec -fno-exceptions -fno-rtti -fshort-wchar \
 *     -D_WIN32=1 -D_M_PPCBE=1 -D_M_PPC=1 -D_XBOX=1 -D_XBOX_VER=200 -D__export= \
 *     -D_SIZE_T_DEFINED -I <xdk>/include/xbox -c smoketest.cpp -o smoketest.o
 *   mktitle.py smoketest.o --lib tracerecording,xsim,xonline,xnet \
 *     --coff-dir build/coff --xdk <xdk>/lib/xbox -o smoketest.xex
 *
 * Probes are ordered safest-first (a hard guest AV aborts the run), so we learn
 * as much as possible before any casualty:
 *   1. XGetLanguage / XGetGameRegion  -- xam.xex system-info imports (implemented
 *      in xenia; no static archive needed)
 *   2. XTraceIsRecording              -- tracerecording.lib, a pure query
 *   3. XSimInitialize / XSimUninitialize -- xsim.lib, controller input sim
 *   4. XOnlineStartup / XOnlineCleanup   -- xonline.lib (needs XNetStartup first)
 */
#include <xtl.h>
#include <tracerecording.h>
#include <xsim.h>
#include <xonline.h>

extern "C" int DbgPrint(const char *, ...);

int main(void)
{
    DbgPrint("[SMOKE] start\n");

    DbgPrint("[SMOKE] 1 xam XGetLanguage before\n");
    DWORD lang = XGetLanguage();
    DbgPrint("[SMOKE] 1 xam XGetLanguage after -> %u\n", lang);

    DbgPrint("[SMOKE] 1 xam XGetGameRegion before\n");
    DWORD region = XGetGameRegion();
    DbgPrint("[SMOKE] 1 xam XGetGameRegion after -> 0x%04X\n", region);

    DbgPrint("[SMOKE] 2 tracerecording XTraceIsRecording before\n");
    BOOL rec = XTraceIsRecording();
    DbgPrint("[SMOKE] 2 tracerecording XTraceIsRecording after -> %d\n", (int)rec);

    DbgPrint("[SMOKE] 3 xsim XSimInitialize before\n");
    HRESULT hsim = XSimInitialize(60);
    DbgPrint("[SMOKE] 3 xsim XSimInitialize after -> 0x%08x\n", hsim);
    if (SUCCEEDED(hsim)) { XSimUninitialize(); DbgPrint("[SMOKE] 3 xsim XSimUninitialize done\n"); }

    DbgPrint("[SMOKE] 4 xnet XNetStartup before\n");
    XNetStartup(NULL);
    DbgPrint("[SMOKE] 4 xnet XNetStartup after\n");

    DbgPrint("[SMOKE] 4 xonline XOnlineStartup before\n");
    DWORD hon = XOnlineStartup();
    DbgPrint("[SMOKE] 4 xonline XOnlineStartup after -> 0x%08x\n", hon);
    if (hon == ERROR_SUCCESS) { XOnlineCleanup(); DbgPrint("[SMOKE] 4 xonline XOnlineCleanup done\n"); }

    DbgPrint("[SMOKE] DONE\n");
    return 0;
}
