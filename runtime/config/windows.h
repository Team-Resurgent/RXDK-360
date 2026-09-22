/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RXDK-360 <windows.h> for the Xbox 360 target.
 *
 * The XDK ships no windows.h at all -- Xbox title code that includes it wants the
 * Win32 subset the console exposes, which the XDK delivers through xtl.h (its
 * Win32-equivalent umbrella: winbase.h, winnt.h, wingdi-less, etc.). The original
 * XDK build only found a windows.h by falling back to the host machine's Platform
 * SDK; we deliberately keep that PC header set off the console include path, so we
 * provide the name here inside our own tree and point it at the XDK umbrella.
 *
 * xtl.h alone is not quite enough: it declares the Win32 file/sync/module APIs as
 * ANSI (*A) only, so title code that keeps WCHAR paths and calls the *W entries
 * does not link. rxdk_win32_wide.h supplies the complete matching wide family
 * (forwarders in runtime/xbox/win32_wide.c), preserving wide-string support
 * without reaching for the system SDK.
 */
#ifndef RXDK_WINDOWS_H
#define RXDK_WINDOWS_H

#include <xtl.h>
#include "rxdk_win32_wide.h"

#endif /* RXDK_WINDOWS_H */
