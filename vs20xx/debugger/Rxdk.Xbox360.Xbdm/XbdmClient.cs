// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace Rxdk.Xbox360.Xbdm
{
    public sealed class XbdmException : Exception
    {
        public int HResult2 { get; }
        public XbdmException(string op, int hr) : base($"{op} failed (0x{(uint)hr:X8})") => HResult2 = hr;
    }

    /// <summary>A debug event reported by the console (text notification), lightly parsed.</summary>
    public sealed class XbdmNotification
    {
        public string Raw = "";
        public string Kind = "";                 // e.g. "break", "exception", "modload", "create", "data"
        public readonly Dictionary<string, string> Fields = new(StringComparer.OrdinalIgnoreCase);
        public uint? Addr => Hex("addr");
        public uint? Thread => Hex("thread");
        private uint? Hex(string k) =>
            Fields.TryGetValue(k, out var v) && TryHex(v, out uint n) ? n : null;
        private static bool TryHex(string s, out uint n)
        {
            s = s.Trim(); if (s.StartsWith("0x") || s.StartsWith("0X")) s = s[2..];
            return uint.TryParse(s, System.Globalization.NumberStyles.HexNumber, null, out n);
        }
        public override string ToString() => Raw;
    }

    /// <summary>
    /// A managed façade over the XDK's xbdm.dll for the RXDK-360 devkit debugger:
    /// connect to a kit, deploy a title, reboot into it stopped for debugging, then
    /// drive execution (breakpoints, stop/go), read/write memory and thread
    /// registers, and receive debug-event notifications. The register PC (Iar) and
    /// memory addresses are guest addresses that map through the title's DWARF
    /// (see Rxdk.Xbox360.Dwarf) to source.
    /// </summary>
    public sealed class XbdmClient : IDisposable
    {
        private IntPtr _connection;
        private IntPtr _notifySession;
        private DmExtNotifyFunction? _notifyDelegate;   // kept alive for the native callback

        /// <summary>Raised (on a native callback thread) for each console debug event.</summary>
        public event Action<XbdmNotification>? Notification;

        static XbdmClient()
        {
            // Load xbdm.dll from the (relocated, side-by-side) XDK rather than relying
            // on it being on PATH, so the debugger is self-locating.
            NativeLibrary.SetDllImportResolver(typeof(XbdmClient).Assembly, Resolve);
        }

        private static IntPtr Resolve(string name, Assembly asm, DllImportSearchPath? path)
        {
            if (!name.Equals("xbdm.dll", StringComparison.OrdinalIgnoreCase)) return IntPtr.Zero;
            foreach (var dir in ProbeDirs())
            {
                string p = Path.Combine(dir, "bin", "x64", "xbdm.dll");
                if (File.Exists(p) && NativeLibrary.TryLoad(p, out var h)) return h;
            }
            return NativeLibrary.TryLoad("xbdm.dll", out var d) ? d : IntPtr.Zero;
        }

        /// <summary>The xbdm.dll the resolver would load (from the XDK/RXDK-360 install), or null.</summary>
        public static string? LocateXbdm()
        {
            foreach (var dir in ProbeDirs())
            {
                string p = Path.Combine(dir, "bin", "x64", "xbdm.dll");
                if (File.Exists(p)) return p;
            }
            return null;
        }

        private static IEnumerable<string> ProbeDirs()
        {
            string? xedk = Environment.GetEnvironmentVariable("XEDK");
            if (!string.IsNullOrEmpty(xedk)) yield return xedk;
            foreach (var (hive, key) in new[]
            {
                (RegistryHive.LocalMachine, @"SOFTWARE\WOW6432Node\TeamResurgent\RXDK-360"),
                (RegistryHive.LocalMachine, @"SOFTWARE\TeamResurgent\RXDK-360"),
                (RegistryHive.LocalMachine, @"SOFTWARE\Wow6432Node\Microsoft\Xbox\2.0\SDK"),
                (RegistryHive.LocalMachine, @"SOFTWARE\Microsoft\Xbox\2.0\SDK"),
            })
            {
                string? v = null;
                try
                {
                    using var b = RegistryKey.OpenBaseKey(hive, RegistryView.Registry64);
                    using var k = b.OpenSubKey(key);
                    v = k?.GetValue("InstallPath") as string;
                }
                catch { }
                if (!string.IsNullOrEmpty(v)) yield return v!;
            }
        }

        private static void Check(string op, int hr) { if (hr < 0) throw new XbdmException(op, hr); }

        // ---- connection ----

        /// <summary>Point the DM library at a console (name or IP) and open a connection.</summary>
        public void Connect(string consoleNameOrIp, uint timeoutMs = 5000)
        {
            Check("DmSetXboxNameNoRegister", NativeXbdm.DmSetXboxNameNoRegister(consoleNameOrIp));
            NativeXbdm.DmSetConnectionTimeout(timeoutMs, timeoutMs);
            Check("DmOpenConnection", NativeXbdm.DmOpenConnection(out _connection));
        }

        /// <summary>Attach the debugger and choose which events halt the title.</summary>
        public void AttachDebugger(uint stopFlags =
            DmStop.CreateThread | DmStop.ModLoad | DmStop.TitleLaunch | DmStop.DebugStr)
        {
            Check("DmConnectDebugger", NativeXbdm.DmConnectDebugger(true));
            Check("DmStopOn", NativeXbdm.DmStopOn(stopFlags, true));
        }

        /// <summary>Open the notification channel; Notification fires for each debug event.</summary>
        public void ListenForEvents()
        {
            const uint DM_DEBUGSESSION = 0x00000002;
            Check("DmOpenNotificationSession", NativeXbdm.DmOpenNotificationSession(DM_DEBUGSESSION, out _notifySession));
            _notifyDelegate = OnNativeNotify;
            Check("DmRegisterNotificationProcessor",
                NativeXbdm.DmRegisterNotificationProcessor(_notifySession, "", _notifyDelegate));
        }

        private uint OnNativeNotify(string text)
        {
            Notification?.Invoke(Parse(text));
            return 0;
        }

        internal static XbdmNotification Parse(string text)
        {
            var n = new XbdmNotification { Raw = text };
            var toks = (text ?? "").Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (toks.Length > 0) n.Kind = toks[0];
            foreach (var t in toks)
            {
                int eq = t.IndexOf('=');
                if (eq > 0) n.Fields[t[..eq]] = t[(eq + 1)..];
            }
            return n;
        }

        // ---- deploy + launch ----

        public void Mkdir(string remoteDir) => NativeXbdm.DmMkdir(remoteDir); // ignore "already exists"
        public void SendFile(string localPath, string remotePath) =>
            Check("DmSendFile", NativeXbdm.DmSendFile(localPath, remotePath));

        /// <summary>Reboot the console into a title, optionally stopped at load for debugging.</summary>
        public void LaunchTitle(string imagePath, string? mediaPath = null, bool stopAtLoad = true, string? cmdLine = null)
        {
            uint flags = DmBoot.Title | (stopAtLoad ? DmBoot.Stop : 0);
            Check("DmRebootEx", NativeXbdm.DmRebootEx(flags, imagePath, mediaPath ?? imagePath, cmdLine));
        }

        // ---- execution control ----

        public void Stop() => Check("DmStop", NativeXbdm.DmStop());
        public void Go() => Check("DmGo", NativeXbdm.DmGo());
        public void SetInitialBreakpoint() => Check("DmSetInitialBreakpoint", NativeXbdm.DmSetInitialBreakpoint());
        public void ContinueThread(uint tid, bool handleException = false) =>
            Check("DmContinueThread", NativeXbdm.DmContinueThread(tid, handleException));

        public void SetBreakpoint(uint address) =>
            Check("DmSetBreakpoint", NativeXbdm.DmSetBreakpoint((IntPtr)address));
        public void RemoveBreakpoint(uint address) =>
            Check("DmRemoveBreakpoint", NativeXbdm.DmRemoveBreakpoint((IntPtr)address));
        public void SetDataBreakpoint(uint address, uint type, uint size) =>
            Check("DmSetDataBreakpoint", NativeXbdm.DmSetDataBreakpoint((IntPtr)address, type, size));

        // ---- threads + registers ----

        public uint[] GetThreads()
        {
            uint count = 0;
            NativeXbdm.DmGetThreadList(null, ref count);      // query count
            if (count == 0) return Array.Empty<uint>();
            var ids = new uint[count];
            Check("DmGetThreadList", NativeXbdm.DmGetThreadList(ids, ref count));
            Array.Resize(ref ids, (int)count);
            return ids;
        }

        public XContext GetContext(uint tid, uint flags = XContextFlags.Full)
        {
            var ctx = XContext.Create(flags);
            Check("DmGetThreadContext", NativeXbdm.DmGetThreadContext(tid, ref ctx));
            return ctx;
        }

        public void SetContext(uint tid, ref XContext ctx) =>
            Check("DmSetThreadContext", NativeXbdm.DmSetThreadContext(tid, ref ctx));

        // ---- memory ----

        public byte[] ReadMemory(uint address, int length)
        {
            var buf = new byte[length];
            Check("DmGetMemory", NativeXbdm.DmGetMemory((IntPtr)address, (uint)length, buf, out uint got));
            if (got != length) Array.Resize(ref buf, (int)got);
            return buf;
        }

        public void WriteMemory(uint address, byte[] data) =>
            Check("DmSetMemory", NativeXbdm.DmSetMemory((IntPtr)address, (uint)data.Length, data, out _));

        public void Dispose()
        {
            if (_notifySession != IntPtr.Zero) { NativeXbdm.DmCloseNotificationSession(_notifySession); _notifySession = IntPtr.Zero; }
            if (_connection != IntPtr.Zero) { NativeXbdm.DmCloseConnection(_connection); _connection = IntPtr.Zero; }
            _notifyDelegate = null;
        }
    }
}
