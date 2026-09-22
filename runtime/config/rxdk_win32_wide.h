/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RXDK-360 Win32 wide (*W) compatibility surface.
 *
 * The Xbox 360 XDK exposes its Win32 file/synchronization/module APIs as ANSI
 * (*A) only -- winbase.h declares CreateFileA, FindFirstFileA, CreateMutexA, ...
 * and #defines the undecorated name to the A entry, with no matching *W. Title
 * code that keeps WCHAR paths (the platform is UTF-16 throughout) therefore calls
 * CreateFileW / SetFileAttributesW / ... which do not exist, and the original XDK
 * build only resolved them by falling back to the host Platform SDK's windows.h
 * -- PC contamination we do not want on the console include path.
 *
 * This header provides the COMPLETE matching wide family for every A/W-split API
 * winbase.h ships (not just the handful any one sample happens to call), so the
 * wide surface is uniform. The forwarders live in runtime/xbox/win32_wide.c and
 * narrow each wide argument to the *A entry the console actually implements
 * (paths on the 360 file systems are ASCII), widening any wide out-parameter
 * back. Included by our <windows.h> (runtime/config/windows.h) AFTER <xtl.h>, so
 * every base type (WCHAR, LPCWSTR, HANDLE, WIN32_FIND_DATAA, FILETIME,
 * GET_FILEEX_INFO_LEVELS, LPPROGRESS_ROUTINE, ...) is already in scope.
 */
#ifndef RXDK_WIN32_WIDE_H
#define RXDK_WIN32_WIDE_H

#ifdef __cplusplus
extern "C" {
#endif

/* WIN32_FIND_DATAW / LPWIN32_FIND_DATAW already come from winbase.h (the XDK
   defines the wide find-data struct even though it ships no wide find function),
   so we only add the missing *W functions below. */

/* ---- synchronization objects (named, LPCWSTR) --------------------------- */
HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCWSTR lpName);
HANDLE WINAPI OpenMutexW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);
HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName);
HANDLE WINAPI OpenEventW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);
HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCWSTR lpName);
HANDLE WINAPI OpenSemaphoreW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);
HANDLE WINAPI CreateWaitableTimerW(LPSECURITY_ATTRIBUTES lpTimerAttributes, BOOL bManualReset, LPCWSTR lpTimerName);
HANDLE WINAPI OpenWaitableTimerW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpTimerName);

/* ---- module / library --------------------------------------------------- */
HMODULE WINAPI LoadLibraryW(LPCWSTR lpLibFileName);
DWORD   WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName);
LPWSTR  WINAPI GetCommandLineW(void);

/* ---- files / directories ------------------------------------------------ */
BOOL   WINAPI GetDiskFreeSpaceExW(LPCWSTR lpDirectoryName, PULARGE_INTEGER lpFreeBytesAvailableToCaller, PULARGE_INTEGER lpTotalNumberOfBytes, PULARGE_INTEGER lpTotalNumberOfFreeBytes);
BOOL   WINAPI CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
BOOL   WINAPI RemoveDirectoryW(LPCWSTR lpPathName);
HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
BOOL   WINAPI SetFileAttributesW(LPCWSTR lpFileName, DWORD dwFileAttributes);
DWORD  WINAPI GetFileAttributesW(LPCWSTR lpFileName);
BOOL   WINAPI GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation);
BOOL   WINAPI DeleteFileW(LPCWSTR lpFileName);
HANDLE WINAPI FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData);
BOOL   WINAPI FindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData);
BOOL   WINAPI CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists);
BOOL   WINAPI CopyFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, LPPROGRESS_ROUTINE lpProgressRoutine, LPVOID lpData, LPBOOL pbCancel, DWORD dwCopyFlags);
BOOL   WINAPI MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName);
BOOL   WINAPI MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags);
BOOL   WINAPI MoveFileWithProgressW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, LPPROGRESS_ROUTINE lpProgressRoutine, LPVOID lpData, DWORD dwFlags);
BOOL   WINAPI GetVolumeInformationW(LPCWSTR lpRootPathName, LPWSTR lpVolumeNameBuffer, DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags, LPWSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize);

#ifdef __cplusplus
}
#endif

#endif /* RXDK_WIN32_WIDE_H */
