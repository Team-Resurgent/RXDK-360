// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.Win32;

namespace Rxdk.Xbox360.Xbdm
{
    public sealed class XbdmException : Exception
    {
        public int Status { get; }
        public XbdmException(string op, int hr) : base(Format(op, hr))
        {
            Status = hr;
            HResult = hr;
        }

        private static string Format(string op, int hr)
        {
            string text = XbdmClient.TryTranslateError(hr);
            return string.IsNullOrEmpty(text)
                ? $"{op} failed (0x{(uint)hr:X8})"
                : $"{op} failed (0x{(uint)hr:X8}): {text}";
        }
    }

    /// <summary>A debug event reported by the console, from DmNotify or the text processor.</summary>
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
        public override string ToString()
        {
            if (!string.IsNullOrEmpty(Raw)) return Raw;
            if (string.IsNullOrEmpty(Kind)) return "";
            var sb = new StringBuilder(Kind);
            foreach (var kv in Fields) sb.Append(' ').Append(kv.Key).Append('=').Append(kv.Value);
            return sb.ToString();
        }
    }

    /// <summary>
    /// Managed façade over the XDK's PC-side xbdm.dll. The host DLL is PE32 and
    /// comes from the installed RXDK-360 product root
    /// (%RXDK360% / InstallPath = {app}\bin\win32\xbdm.dll). Stock %XEDK% is
    /// only a fallback when RXDK-360 is not installed.
    /// </summary>
    public sealed class XbdmClient : IDisposable
    {
        private static readonly uint[] NotifyKinds =
        {
            DmNotification.Break, DmNotification.SingleStep, DmNotification.DataBreak,
            DmNotification.Exception, DmNotification.DebugStr, DmNotification.Exec,
            DmNotification.ModLoad, DmNotification.ModUnload, DmNotification.CreateThread,
            DmNotification.DestroyThread, DmNotification.Assert, DmNotification.Rip,
            DmNotification.BugCheck,
        };

        private IntPtr _connection;
        private IntPtr _notifySession;
        private DmExtNotifyFunction? _extNotify;
        private DmNotifyFunction? _typedNotify;
        private string _console = "";
        private bool _debuggerAttached;
        private readonly ConcurrentQueue<XbdmNotification> _events = new();
        private readonly AutoResetEvent _eventPulse = new(false);

        /// <summary>Raised (on a native callback thread) for each console debug event.</summary>
        public event Action<XbdmNotification>? Notification;

        static XbdmClient()
        {
            if (Environment.Is64BitProcess)
            {
                throw new InvalidOperationException(
                    "Rxdk.Xbox360.Xbdm must run as x86 so it can load bin\\win32\\xbdm.dll.");
            }
            NativeLibrary.SetDllImportResolver(typeof(XbdmClient).Assembly, Resolve);
        }

        private static IntPtr Resolve(string name, Assembly asm, DllImportSearchPath? path)
        {
            if (!name.Equals("xbdm.dll", StringComparison.OrdinalIgnoreCase)) return IntPtr.Zero;
            string? located = LocateXbdm();
            if (located != null && NativeLibrary.TryLoad(located, out var h)) return h;
            return NativeLibrary.TryLoad("xbdm.dll", out var d) ? d : IntPtr.Zero;
        }

        /// <summary>
        /// Host xbdm.dll from the installed RXDK-360
        /// (<c>%RXDK360%</c> / HKLM TeamResurgent\RXDK-360\InstallPath =
        /// <c>{app}\bin\win32\xbdm.dll</c>). Stock %XEDK% is last-resort only.
        /// </summary>
        public static string? LocateXbdm()
        {
            foreach (var dir in ProbeDirs())
            {
                string p = Path.Combine(dir, "bin", "win32", "xbdm.dll");
                if (File.Exists(p)) return p;
            }
            return null;
        }

        /// <summary>
        /// $(DefaultConsole) from the XDK Neighborhood name
        /// (HKCU\SOFTWARE\Microsoft\XenonSDK\XboxName).
        /// </summary>
        public static string? DefaultConsole()
        {
            try
            {
                using var k = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Microsoft\XenonSDK");
                string? name = k?.GetValue("XboxName") as string;
                if (!string.IsNullOrWhiteSpace(name)) return name;
            }
            catch { }
            return null;
        }

        private static IEnumerable<string> ProbeDirs()
        {
            // RXDK-360 first. devenv often has stock %XEDK% and may not inherit
            // %RXDK360%; do not let the Microsoft Xbox 360 SDK jump the queue.
            foreach (var dir in Rxdk360Roots())
                yield return dir;

            string? xedk = Environment.GetEnvironmentVariable("XEDK");
            if (!string.IsNullOrEmpty(xedk)) yield return xedk;
            foreach (var key in new[]
            {
                @"SOFTWARE\Wow6432Node\Microsoft\Xbox\2.0\SDK",
                @"SOFTWARE\Microsoft\Xbox\2.0\SDK",
            })
            {
                string? inst = ReadReg32(key, "InstallPath");
                if (!string.IsNullOrEmpty(inst)) yield return inst;
            }
            yield return @"C:\Program Files (x86)\Microsoft Xbox 360 SDK";
        }

        private static IEnumerable<string> Rxdk360Roots()
        {
            string? env = Environment.GetEnvironmentVariable("RXDK360");
            if (!string.IsNullOrEmpty(env)) yield return env;

            foreach (var key in new[]
            {
                @"SOFTWARE\WOW6432Node\TeamResurgent\RXDK-360",
                @"SOFTWARE\TeamResurgent\RXDK-360",
            })
            {
                string? inst = ReadReg32(key, "InstallPath");
                if (!string.IsNullOrEmpty(inst)) yield return inst;
            }

            yield return @"C:\Program Files\RXDK-360";
        }

        private static string? ReadReg32(string subkey, string name)
        {
            try
            {
                using var b = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry32);
                using var k = b.OpenSubKey(subkey);
                return k?.GetValue(name) as string;
            }
            catch { return null; }
        }

        internal static string TryTranslateError(int hr)
        {
            try
            {
                var sb = new StringBuilder(512);
                if (NativeXbdm.DmTranslateError(hr, sb, sb.Capacity) >= 0)
                    return sb.ToString();
            }
            catch { }
            return "";
        }

        private static void Check(string op, int hr)
        {
            if (!XbdmHResults.IsSuccess(hr)) throw new XbdmException(op, hr);
        }

        private static UIntPtr Xbox(uint address) => new(address);
        private static uint Xbox32(UIntPtr p) => unchecked((uint)p.ToUInt32());

        /// <summary>Default preferred load / link base for both the .exe and the .xex.</summary>
        public const uint DefaultXexBase = 0x82000000;

        public bool TryFindModule(string hint, out uint baseAddress, out uint size, out string name)
        {
            baseAddress = 0; size = 0; name = "";
            foreach (var m in WalkLoadedModules())
            {
                if (string.IsNullOrEmpty(m.Name) ||
                    m.Name.IndexOf(hint, StringComparison.OrdinalIgnoreCase) < 0)
                    continue;
                baseAddress = Xbox32(m.BaseAddress);
                size = m.Size;
                name = m.Name;
                return true;
            }
            return false;
        }

        // ---- connection ----

        /// <summary>Point the DM library at a console (name or IP) and open a connection.</summary>
        public void Connect(string consoleNameOrIp, uint timeoutMs = 5000)
        {
            _console = consoleNameOrIp;
            NativeXbdm.DmUseSharedConnection(true);
            Check("DmSetXboxNameNoRegister", NativeXbdm.DmSetXboxNameNoRegister(consoleNameOrIp));
            NativeXbdm.DmSetConnectionTimeout(timeoutMs, timeoutMs);
            Check("DmOpenConnection", NativeXbdm.DmOpenConnection(out _connection));
        }

        /// <summary>Attach the debugger. Default StopOn is off — CreateThread/DebugStr/FCE
        /// re-halt a running D3D title (and Present hangs while a debugger stays connected).</summary>
        public void AttachDebugger(uint stopFlags = 0)
        {
            int hr = NativeXbdm.DmConnectDebugger(true);
            if (!XbdmHResults.IsSuccess(hr) && hr != XbdmHResults.AlreadyExists)
                throw new XbdmException("DmConnectDebugger", hr);
            _debuggerAttached = true;
            if (stopFlags != 0)
                Check("DmStopOn", NativeXbdm.DmStopOn(stopFlags, true));
        }

        public void DetachDebugger()
        {
            NativeXbdm.DmStopOn(DmStop.All, false);
            NativeXbdm.DmConnectDebugger(false);
            _debuggerAttached = false;
        }

        public void StopOn(uint flags, bool stop) =>
            Check("DmStopOn", NativeXbdm.DmStopOn(flags, stop));

        /// <summary>Continue every stopped thread and DmGo, keeping the debugger attached.</summary>
        public void ContinueAll(bool handleException = true)
        {
            uint[] threads;
            try { threads = GetThreads(); }
            catch { threads = Array.Empty<uint>(); }
            foreach (var tid in threads)
            {
                try { ContinueThread(tid, handleException); }
                catch { }
            }
            Go();
        }

        /// <summary>Continue every stopped thread, DmGo, and optionally detach so D3D can Present.</summary>
        public void Resume(bool detach = false)
        {
            int hr = NativeXbdm.DmConnectDebugger(true);
            if (!XbdmHResults.IsSuccess(hr) && hr != XbdmHResults.AlreadyExists)
                throw new XbdmException("DmConnectDebugger", hr);
            _debuggerAttached = true;
            NativeXbdm.DmStopOn(DmStop.All, false);
            ContinueAll();
            if (detach)
                DetachDebugger();
        }

        /// <summary>Open the notification channel; Notification fires for each debug event.</summary>
        public void ListenForEvents(uint flags = DmNotifySession.DebugSession)
        {
            Check("DmOpenNotificationSession",
                NativeXbdm.DmOpenNotificationSession(flags, out _notifySession));
            _typedNotify = OnTypedNotify;
            foreach (uint kind in NotifyKinds)
                Check("DmNotify", NativeXbdm.DmNotify(_notifySession, kind, _typedNotify));
            _extNotify = OnExtNotify;
            Check("DmRegisterNotificationProcessor",
                NativeXbdm.DmRegisterNotificationProcessor(_notifySession, "", _extNotify));
        }

        private uint OnExtNotify(string text)
        {
            Raise(Parse(text));
            return 0;
        }

        private uint OnTypedNotify(uint notification, UIntPtr param)
        {
            Raise(FromTyped(notification, param));
            return 0;
        }

        private void Raise(XbdmNotification n)
        {
            _events.Enqueue(n);
            _eventPulse.Set();
            Notification?.Invoke(n);
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

        private static XbdmNotification FromTyped(uint notification, UIntPtr param)
        {
            uint kind = notification & DmNotification.Mask;
            var n = new XbdmNotification();
            IntPtr p = unchecked((IntPtr)(long)param.ToUInt64());
            try
            {
                switch (kind)
                {
                    case DmNotification.Break:
                    case DmNotification.SingleStep:
                    {
                        var s = Marshal.PtrToStructure<DmnBreak>(p);
                        n.Kind = kind == DmNotification.SingleStep ? "singlestep" : "break";
                        n.Fields["addr"] = $"0x{Xbox32(s.Address):X8}";
                        n.Fields["thread"] = $"0x{s.ThreadId:X8}";
                        break;
                    }
                    case DmNotification.DataBreak:
                    {
                        var s = Marshal.PtrToStructure<DmnDataBreak>(p);
                        n.Kind = "data";
                        n.Fields["addr"] = $"0x{Xbox32(s.Address):X8}";
                        n.Fields["thread"] = $"0x{s.ThreadId:X8}";
                        break;
                    }
                    case DmNotification.Exception:
                    {
                        var s = Marshal.PtrToStructure<DmnException>(p);
                        n.Kind = "exception";
                        n.Fields["addr"] = $"0x{Xbox32(s.Address):X8}";
                        n.Fields["thread"] = $"0x{s.ThreadId:X8}";
                        n.Fields["code"] = $"0x{s.Code:X8}";
                        break;
                    }
                    case DmNotification.DebugStr:
                    case DmNotification.Assert:
                    {
                        var s = Marshal.PtrToStructure<DmnDebugStr>(p);
                        n.Kind = kind == DmNotification.Assert ? "assert" : "debugstr";
                        n.Fields["thread"] = $"0x{s.ThreadId:X8}";
                        if (s.String != IntPtr.Zero)
                            n.Fields["string"] = Marshal.PtrToStringAnsi(s.String, (int)s.Length) ?? "";
                        break;
                    }
                    case DmNotification.ModLoad:
                    case DmNotification.ModUnload:
                    {
                        var s = Marshal.PtrToStructure<DmnModLoad>(p);
                        n.Kind = kind == DmNotification.ModLoad ? "modload" : "modunload";
                        n.Fields["name"] = s.Name ?? "";
                        n.Fields["addr"] = $"0x{Xbox32(s.BaseAddress):X8}";
                        n.Fields["thread"] = $"0x{s.ThreadId:X8}";
                        break;
                    }
                    case DmNotification.CreateThread:
                    {
                        var s = Marshal.PtrToStructure<DmnCreateThread>(p);
                        n.Kind = "create";
                        n.Fields["thread"] = $"0x{s.ThreadId:X8}";
                        n.Fields["addr"] = $"0x{Xbox32(s.StartAddress):X8}";
                        break;
                    }
                    case DmNotification.DestroyThread:
                        n.Kind = "destroy";
                        n.Fields["thread"] = $"0x{Xbox32(param):X8}";
                        break;
                    case DmNotification.Exec:
                        n.Kind = "exec";
                        n.Fields["state"] = param.ToUInt32().ToString();
                        break;
                    case DmNotification.Rip:
                        n.Kind = "rip";
                        break;
                    case DmNotification.BugCheck:
                        n.Kind = "bugcheck";
                        break;
                    default:
                        n.Kind = $"notify{kind}";
                        break;
                }
            }
            catch
            {
                n.Kind = $"notify{kind}";
            }
            n.Raw = n.Kind;
            foreach (var kv in n.Fields) n.Raw += $" {kv.Key}={kv.Value}";
            return n;
        }

        // ---- deploy + launch ----

        public void Mkdir(string remoteDir) => NativeXbdm.DmMkdir(remoteDir); // ignore "already exists"
        public void SendFile(string localPath, string remotePath) =>
            Check("DmSendFile", NativeXbdm.DmSendFile(localPath, remotePath));

        /// <summary>Capture the front buffer to a BMP on the PC (DmScreenShot).</summary>
        public void ScreenShot(string localBmpPath)
        {
            string dir = Path.GetDirectoryName(localBmpPath) ?? "";
            if (dir.Length != 0) Directory.CreateDirectory(dir);
            NativeXbdm.DmSetConnectionTimeout(30_000, 30_000);
            Check("DmScreenShot", NativeXbdm.DmScreenShot(localBmpPath));
        }

        public void SetTitle(string dir, string title, string? cmdLine = null) =>
            Check("DmSetTitle", NativeXbdm.DmSetTitle(dir, title, cmdLine));

        /// <summary>Copy a local XEX to the kit under remoteDir (default: the VS RemoteRoot layout).</summary>
        public string DeployTitle(string localXex, string remoteDir)
        {
            string d = remoteDir.Replace('/', '\\');
            if (!d.EndsWith('\\')) d += '\\';
            string accum = "";
            foreach (var part in d.Split('\\', StringSplitOptions.RemoveEmptyEntries))
            {
                accum = accum.Length == 0 ? part + "\\" : accum + part + "\\";
                Mkdir(accum.TrimEnd('\\'));
            }
            string remote = d + Path.GetFileName(localXex);
            SendFile(localXex, remote);
            return remote;
        }

        /// <summary>Reboot the console into a title. stopAtLoad is ignored for the reboot
        /// flags — DMBOOT_STOP is pending-exec, used by <see cref="LaunchStopped"/>, not
        /// a title reboot (title+STOP leaves EXEC_STOP, which then ignores the next reboot).</summary>
        public void LaunchTitle(string imagePath, string? mediaPath = null, bool stopAtLoad = false, string? cmdLine = null, bool cold = false)
        {
            uint flags = DmBoot.Title | DmBoot.Wait | (cold ? DmBoot.Cold : 0);
            NativeXbdm.DmSetConnectionTimeout(120_000, 120_000);
            // The 3rd DmRebootEx arg is the media root that GAME: maps to. Default it to
            // the title's DIRECTORY, not the .xex file -- pointing GAME: at the .xex makes
            // ObCreateSymbolicLink fail with STATUS_NOT_A_DIRECTORY (0xC0000103), so
            // game:\Media\... never resolves and any content load (shaders, textures)
            // fails in Initialize().
            string media = mediaPath ?? XeDirOf(imagePath);
            Check("DmRebootEx", NativeXbdm.DmRebootEx(flags, imagePath, media, cmdLine));
        }

        // Directory portion of an Xbox path (devkit:\Foo\Foo.xex -> devkit:\Foo).
        private static string XeDirOf(string xePath)
        {
            int i = xePath.Replace('/', '\\').LastIndexOf('\\');
            return i > 0 ? xePath.Substring(0, i) : xePath;
        }

        public void Reboot(uint flags)
        {
            NativeXbdm.DmSetConnectionTimeout(120_000, 120_000);
            Check("DmReboot", NativeXbdm.DmReboot(flags));
        }

        /// <summary>
        /// Official VS F5: title-reboot (TITLE|WAIT, not COLD), stop on the xbdm
        /// initial break (BREAK START armed at PENDING_TITLE), then the IDE sets
        /// source breakpoints and auto-continues. This method only does the wait;
        /// the caller plants BPs and Go()s.
        /// </summary>
        public XbdmNotification LaunchStopped(string remoteOrLocalXex, TimeSpan? timeout = null, string? remoteDir = null)
        {
            var wait = timeout ?? TimeSpan.FromSeconds(90);
            string remote = remoteOrLocalXex;
            if (File.Exists(remoteOrLocalXex))
            {
                string dir = remoteDir ?? ("devkit:\\" + Path.GetFileNameWithoutExtension(remoteOrLocalXex));
                remote = DeployTitle(remoteOrLocalXex, dir);
            }
            remote = ToXePath(remote);

            var (titleDir, titleName) = SplitRemote(remote);
            string titleBase = Path.GetFileNameWithoutExtension(titleName);

            // Official F5 keeps the XBDM connection up across title reboot and catches
            // the xbdm.xex initial break. Do not Disconnect() here — that misses it.
            if (_notifySession == IntPtr.Zero) ListenForEvents();
            try { NativeXbdm.DmStopOn(DmStop.All, false); } catch { }
            try { Go(); } catch { }

            SetTitle(titleDir, titleName);
            AttachDebugger();
            try { Stop(); } catch { }
            try { SetInitialBreakpoint(); } catch (XbdmException) { }

            Notification += ArmInitialBreakpointOnPending;
            try
            {
                while (_events.TryDequeue(out _)) { }
                try
                {
                    LaunchTitle(remote, stopAtLoad: false, cold: false);
                }
                catch (XbdmException)
                {
                    try
                    {
                        SettleConnection();
                        if (_notifySession == IntPtr.Zero) ListenForEvents();
                        AttachDebugger();
                    }
                    catch { }
                }

                var deadline = DateTime.UtcNow + wait;
                XbdmNotification? last = null;
                while (DateTime.UtcNow < deadline)
                {
                    try
                    {
                        if (WaitFor(n => IsEntryStop(n) || n.Kind is "exec" or "modload", TimeSpan.FromMilliseconds(400), out var n))
                            last = n;

                        if (last != null && IsEntryStop(last))
                        {
                            try { StopOn(DmStop.All, false); } catch { }
                            return last;
                        }
                    }
                    catch (XbdmException)
                    {
                        try { Reconnect(_console, 10_000); if (_notifySession == IntPtr.Zero) ListenForEvents(); }
                        catch { }
                    }
                }
                throw new TimeoutException($"timed out waiting for {titleName} initial break" +
                    (last != null ? $" (last {last})" : ""));
            }
            finally
            {
                Notification -= ArmInitialBreakpointOnPending;
            }
        }

        /// <summary>Official F5 arms BREAK START during PENDING_TITLE (state 5), a few ms before START.</summary>
        private void ArmInitialBreakpointOnPending(XbdmNotification n)
        {
            if (n.Kind != "exec") return;
            if (!n.Fields.TryGetValue("state", out var s) || s is not ("3" or "5")) return;
            NativeXbdm.DmSetInitialBreakpoint();
        }

        /// <summary>Wait until the kit drops off XBDM (reboot started) then accept a new connection.</summary>
        private void WaitForConsoleDropAndReturn(TimeSpan timeout, bool acceptImmediate = false)
        {
            Disconnect();
            var deadline = DateTime.UtcNow + timeout;
            bool dropped = false;
            while (DateTime.UtcNow < deadline)
            {
                try
                {
                    Connect(_console, 1_500);
                    if (dropped || acceptImmediate)
                        return;
                    Disconnect();
                    Thread.Sleep(250);
                }
                catch
                {
                    dropped = true;
                    acceptImmediate = false;
                    Thread.Sleep(400);
                }
            }
            Reconnect(_console, (uint)Math.Max(5_000, timeout.TotalMilliseconds));
        }

        /// <summary>Two successful connects in a row — the kit is up, not still bouncing.</summary>
        private void SettleConnection()
        {
            int ok = 0;
            var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(30);
            while (DateTime.UtcNow < deadline && ok < 2)
            {
                try
                {
                    Reconnect(_console, 5_000);
                    ok++;
                    Thread.Sleep(400);
                }
                catch
                {
                    ok = 0;
                    Thread.Sleep(400);
                }
            }
            if (_connection == IntPtr.Zero)
                Reconnect(_console, 15_000);
        }

        private void AfterDrop(Action op)
        {
            XbdmException? last = null;
            for (int i = 0; i < 6; i++)
            {
                try
                {
                    op();
                    return;
                }
                catch (XbdmException ex)
                {
                    last = ex;
                    try
                    {
                        Reconnect(_console, 10_000);
                        if (_notifySession == IntPtr.Zero) ListenForEvents();
                    }
                    catch { }
                    Thread.Sleep(500);
                }
            }
            if (last != null) throw last;
        }

        private static string ToXePath(string path)
        {
            path = path.Replace('/', '\\');
            if (path.StartsWith("devkit:", StringComparison.OrdinalIgnoreCase))
                return "xe:" + path[7..];
            return path;
        }

        private bool TitleModuleLoaded(string titleBase)
        {
            try
            {
                foreach (var m in WalkLoadedModules())
                {
                    if (!string.IsNullOrEmpty(m.Name) &&
                        m.Name.IndexOf(titleBase, StringComparison.OrdinalIgnoreCase) >= 0)
                        return true;
                }
            }
            catch { }
            return false;
        }

        public void Disconnect()
        {
            if (_notifySession != IntPtr.Zero)
            {
                try { NativeXbdm.DmCloseNotificationSession(_notifySession); } catch { }
                _notifySession = IntPtr.Zero;
            }
            if (_connection != IntPtr.Zero)
            {
                try { NativeXbdm.DmCloseConnection(_connection); } catch { }
                _connection = IntPtr.Zero;
            }
            _extNotify = null;
            _typedNotify = null;
        }

        public void Reconnect(string? console = null, uint timeoutMs = 30_000)
        {
            string name = console ?? _console;
            Disconnect();
            var deadline = DateTime.UtcNow + TimeSpan.FromMilliseconds(timeoutMs);
            Exception? last = null;
            while (DateTime.UtcNow < deadline)
            {
                try { Connect(name, Math.Min(timeoutMs, 5_000)); return; }
                catch (Exception ex) { last = ex; Thread.Sleep(500); }
            }
            throw last ?? new XbdmException("Reconnect", XbdmHResults.CannotConnect);
        }

        public bool WaitFor(Func<XbdmNotification, bool> match, TimeSpan timeout, out XbdmNotification hit)
        {
            var deadline = DateTime.UtcNow + timeout;
            hit = null!;
            while (true)
            {
                while (_events.TryDequeue(out var n))
                {
                    if (match(n)) { hit = n; return true; }
                }
                var left = deadline - DateTime.UtcNow;
                if (left <= TimeSpan.Zero) return false;
                _eventPulse.WaitOne(left);
            }
        }

        private static bool IsStopEvent(XbdmNotification n) =>
            n.Kind is "break" or "singlestep" or "data" or "exception" or "create";

        private static bool IsEntryStop(XbdmNotification n) =>
            n.Kind is "break" or "singlestep" or "data" or "exception";

        private bool TrySnapshotStopped(out XbdmNotification hit)
        {
            hit = null!;
            try
            {
                foreach (var tid in GetThreads())
                {
                    if (!TryGetThreadStop(tid, out var stop)) continue;
                    hit = new XbdmNotification { Kind = "break" };
                    hit.Fields["thread"] = $"0x{tid:X8}";
                    hit.Fields["reason"] = stop.NotifiedReason.ToString();
                    return true;
                }
            }
            catch { }
            return false;
        }

        private bool TryTitleStop(string titleBase, out XbdmNotification hit)
        {
            hit = null!;
            uint titleBaseAddr = 0, titleSize = 0;
            foreach (var m in WalkLoadedModules())
            {
                if (string.IsNullOrEmpty(m.Name) ||
                    m.Name.IndexOf(titleBase, StringComparison.OrdinalIgnoreCase) < 0)
                    continue;
                titleBaseAddr = Xbox32(m.BaseAddress);
                titleSize = m.Size;
                break;
            }
            if (titleSize == 0) return false;

            bool InTitle(uint addr) => addr >= titleBaseAddr && addr < titleBaseAddr + titleSize;
            try
            {
                foreach (var tid in GetThreads())
                {
                    if (!TryGetThreadStop(tid, out var stop)) continue;
                    uint start = 0, pc = 0;
                    try { start = Xbox32(GetThreadInfo(tid).StartAddress); } catch { }
                    try { pc = (uint)GetContext(tid, XContextFlags.Control | XContextFlags.Integer).Iar; } catch { }
                    if (!InTitle(start) && !InTitle(pc)) continue;
                    hit = new XbdmNotification { Kind = "break", Raw = $"{titleBase} thread=0x{tid:X8} pc=0x{pc:X8} start=0x{start:X8}" };
                    hit.Fields["thread"] = $"0x{tid:X8}";
                    hit.Fields["addr"] = $"0x{pc:X8}";
                    hit.Fields["start"] = $"0x{start:X8}";
                    hit.Fields["reason"] = stop.NotifiedReason.ToString();
                    return true;
                }
            }
            catch { }
            return false;
        }

        private static (string dir, string name) SplitRemote(string remote)
        {
            remote = remote.Replace('/', '\\');
            int i = remote.LastIndexOf('\\');
            if (i < 0) return ("xe:\\", remote);
            string dir = remote[..(i + 1)];
            return (dir, remote[(i + 1)..]);
        }

        // ---- execution control ----

        public void Stop() => Check("DmStop", NativeXbdm.DmStop());
        public void Go()
        {
            int hr = NativeXbdm.DmGo();
            if (!XbdmHResults.IsSuccess(hr) && hr != XbdmHResults.NotStopped)
                throw new XbdmException("DmGo", hr);
        }
        public void SetInitialBreakpoint() => Check("DmSetInitialBreakpoint", NativeXbdm.DmSetInitialBreakpoint());
        public void ContinueThread(uint tid, bool handleException = false)
        {
            int hr = NativeXbdm.DmContinueThread(tid, handleException);
            if (!XbdmHResults.IsSuccess(hr) && hr != XbdmHResults.NotStopped && hr != XbdmHResults.NoThread)
                throw new XbdmException("DmContinueThread", hr);
        }

        public void SetBreakpoint(uint address) =>
            Check("DmSetBreakpoint", NativeXbdm.DmSetBreakpoint(Xbox(address)));
        public void RemoveBreakpoint(uint address) =>
            Check("DmRemoveBreakpoint", NativeXbdm.DmRemoveBreakpoint(Xbox(address)));
        public void SetDataBreakpoint(uint address, uint type, uint size) =>
            Check("DmSetDataBreakpoint", NativeXbdm.DmSetDataBreakpoint(Xbox(address), type, size));
        public uint GetBreakpointType(uint address)
        {
            Check("DmIsBreakpoint", NativeXbdm.DmIsBreakpoint(Xbox(address), out uint type));
            return type;
        }

        // ---- threads + registers ----

        public uint[] GetThreads()
        {
            uint count = 256;
            var ids = new uint[count];
            int hr = NativeXbdm.DmGetThreadList(ids, ref count);
            if (hr == XbdmHResults.BufferTooSmall)
            {
                ids = new uint[Math.Max(count, 1)];
                hr = NativeXbdm.DmGetThreadList(ids, ref count);
            }
            Check("DmGetThreadList", hr);
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

        public DmThreadInfoEx GetThreadInfo(uint tid)
        {
            var info = new DmThreadInfoEx { Size = (uint)Marshal.SizeOf<DmThreadInfoEx>() };
            Check("DmGetThreadInfoEx", NativeXbdm.DmGetThreadInfoEx(tid, ref info));
            return info;
        }

        public bool TryGetThreadStop(uint tid, out DmThreadStop stop)
        {
            int hr = NativeXbdm.DmIsThreadStopped(tid, out stop);
            if (hr == XbdmHResults.NotStopped) return false;
            Check("DmIsThreadStopped", hr);
            return true;
        }

        public List<DmnModLoad> WalkLoadedModules()
        {
            var list = new List<DmnModLoad>();
            IntPtr walk = IntPtr.Zero;
            try
            {
                while (true)
                {
                    int hr = NativeXbdm.DmWalkLoadedModules(ref walk, out var mod);
                    if (hr == XbdmHResults.EndOfList) break;
                    Check("DmWalkLoadedModules", hr);
                    list.Add(mod);
                }
            }
            finally
            {
                if (walk != IntPtr.Zero) NativeXbdm.DmCloseLoadedModules(walk);
            }
            return list;
        }

        // ---- memory ----

        public byte[] ReadMemory(uint address, int length)
        {
            var buf = new byte[length];
            Check("DmGetMemory", NativeXbdm.DmGetMemory(Xbox(address), (uint)length, buf, out uint got));
            if (got != length) Array.Resize(ref buf, (int)got);
            return buf;
        }

        public void WriteMemory(uint address, byte[] data) =>
            Check("DmSetMemory", NativeXbdm.DmSetMemory(Xbox(address), (uint)data.Length, data, out _));

        /// <summary>
        /// When true (CLI default), Dispose continues the title and detaches so D3D can Present.
        /// The DAP session sets this false so Stop Debugging can reboot instead of leaving the title running.
        /// </summary>
        public bool ResumeOnDispose { get; set; } = true;

        /// <summary>Halt the title and warm-reboot to the dashboard (VS Stop Debugging).</summary>
        public void RebootToDashboard()
        {
            _debuggerAttached = false;
            NativeXbdm.DmSetConnectionTimeout(8_000, 8_000);
            // A crashed title sits halted at its unhandled exception. Just DmStop +
            // DmReboot leaves the console frozen there and the reboot never takes, so
            // Stop appears to hang and never returns to the dashboard. First release
            // the faulted threads (continue WITHOUT handling, so the exception runs its
            // course and the title terminates), then reboot.
            try { foreach (var tid in GetThreads()) try { NativeXbdm.DmContinueThread(tid, false); } catch { } } catch { }
            try { NativeXbdm.DmGo(); } catch { }
            // Warm reboot returns to the dashboard (the debugged title is not relaunched).
            int hr = -1;
            try { hr = NativeXbdm.DmReboot(DmBoot.Warm); } catch { }
            // If the warm reboot was blocked by a wedged title, force a cold reboot,
            // which always returns the console to the dashboard.
            if (!XbdmHResults.IsSuccess(hr))
                try { NativeXbdm.DmReboot(DmBoot.Cold); } catch { }
        }

        public void Dispose()
        {
            if (ResumeOnDispose && _debuggerAttached)
            {
                try { Resume(detach: true); } catch { }
            }
            Disconnect();
            _eventPulse.Dispose();
        }
    }
}
