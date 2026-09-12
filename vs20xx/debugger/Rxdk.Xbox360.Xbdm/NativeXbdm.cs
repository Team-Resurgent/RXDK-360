// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// P/Invoke surface for the XDK's PC-side debug monitor library (xbdm.dll). We use
// the shipped xbdm.dll rather than reimplementing the XBDM wire protocol; these
// are the DM* entry points the RXDK-360 devkit debugger drives. All are __stdcall
// and return an HRESULT (XBDM_* codes; SUCCEEDED() means OK, XBDM_CONNECTED too).

using System;
using System.Runtime.InteropServices;

namespace Rxdk.Xbox360.Xbdm
{
    /// <summary>Callback for extended (text) notifications - debug events arrive as
    /// strings (e.g. "break addr=0x... thread=..."), parsed by the client.</summary>
    [UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    public delegate uint DmExtNotifyFunction([MarshalAs(UnmanagedType.LPStr)] string notification);

    internal static class NativeXbdm
    {
        private const string Dll = "xbdm.dll";
        private const CallingConvention Cc = CallingConvention.StdCall;

        // ---- connection ----
        [DllImport(Dll, CallingConvention = Cc, CharSet = CharSet.Ansi)]
        public static extern int DmSetXboxNameNoRegister(string name);
        [DllImport(Dll, CallingConvention = Cc, CharSet = CharSet.Ansi)]
        public static extern int DmSetXboxName(string name);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmSetConnectionTimeout(uint connectTimeout, uint conversationTimeout);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmOpenConnection(out IntPtr connection);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmCloseConnection(IntPtr connection);

        // ---- debugger session ----
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmConnectDebugger([MarshalAs(UnmanagedType.Bool)] bool connect);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmIsDebuggerPresent();
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmStopOn(uint stopFlags, [MarshalAs(UnmanagedType.Bool)] bool stop);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmSetInitialBreakpoint();

        // ---- notifications ----
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmOpenNotificationSession(uint flags, out IntPtr session);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmCloseNotificationSession(IntPtr session);
        [DllImport(Dll, CallingConvention = Cc, CharSet = CharSet.Ansi)]
        public static extern int DmRegisterNotificationProcessor(IntPtr session, string type, DmExtNotifyFunction pfn);

        // ---- execution control ----
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmStop();
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmGo();
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmHaltThread(uint threadId);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmContinueThread(uint threadId, [MarshalAs(UnmanagedType.Bool)] bool handleException);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmSuspendThread(uint threadId);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmResumeThread(uint threadId);

        // ---- breakpoints ----
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmSetBreakpoint(IntPtr addr);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmRemoveBreakpoint(IntPtr addr);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmSetDataBreakpoint(IntPtr addr, uint type, uint size);

        // ---- memory ----
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmGetMemory(IntPtr addr, uint cb, byte[] buf, out uint cbRet);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmSetMemory(IntPtr addr, uint cb, byte[] buf, out uint cbRet);

        // ---- threads ----
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmGetThreadList([Out] uint[]? threads, ref uint count);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmGetThreadContext(uint threadId, ref XContext context);
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmSetThreadContext(uint threadId, ref XContext context);

        // ---- files / deploy ----
        [DllImport(Dll, CallingConvention = Cc, CharSet = CharSet.Ansi, EntryPoint = "DmSendFileA")]
        public static extern int DmSendFile(string localName, string remoteName);
        [DllImport(Dll, CallingConvention = Cc, CharSet = CharSet.Ansi)]
        public static extern int DmMkdir(string directoryName);

        // ---- reboot / launch ----
        [DllImport(Dll, CallingConvention = Cc)]
        public static extern int DmReboot(uint flags);
        [DllImport(Dll, CallingConvention = Cc, CharSet = CharSet.Ansi)]
        public static extern int DmRebootEx(uint flags, string? imagePath, string? mediaPath, string? dbgCmdLine);
    }

    /// <summary>XBDM stop-reason flags (DmStopOn).</summary>
    public static class DmStop
    {
        public const uint CreateThread = 0x00000001;
        public const uint Fce = 0x00000002;       // first-chance exception
        public const uint DebugStr = 0x00000004;
        public const uint StackTrace = 0x00000008;
        public const uint ModLoad = 0x00000010;
        public const uint TitleLaunch = 0x00000020;
    }

    /// <summary>Reboot flags (DmReboot / DmRebootEx).</summary>
    public static class DmBoot
    {
        public const uint Title = 0;
        public const uint Wait = 1;
        public const uint Cold = 4;
        public const uint Stop = 8;   // stop at load, for debugging
    }

    /// <summary>Data-breakpoint types (DmSetDataBreakpoint).</summary>
    public static class DmBreak
    {
        public const uint None = 0, Write = 1, ReadWrite = 2, Execute = 3, Fixed = 4, Read = 5;
    }
}
