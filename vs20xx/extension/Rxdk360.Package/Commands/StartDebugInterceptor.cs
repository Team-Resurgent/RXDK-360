// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.IO;
using System.Threading;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.OLE.Interop;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Rxdk360.Package.Services;
using Task = System.Threading.Tasks.Task;

namespace Rxdk360.Package.Commands
{
    /// <summary>
    /// First crack at Debug.Start / StartWithoutDebugging. Only Copy to Hard Drive
    /// Xbox 360 projects are handled; Xenia and Emulate DVD fall through to VS.
    ///
    /// Do not query SolutionBuild / ActiveConfiguration from Exec — VS is still
    /// inside Debug.Start and that raises FindActiveProjectCfgName E_UNEXPECTED.
    /// </summary>
    internal sealed class StartDebugInterceptor : IOleCommandTarget
    {
        private readonly AsyncPackage _package;
        private int _reentry;
        private int _queued;
        private int _passthroughStart;
        // COM event sinks must be fields or they are collected and stop firing.
        private EnvDTE.Events _dteEvents;
        private EnvDTE.CommandEvents _cmdAll;

        private StartDebugInterceptor(AsyncPackage package) => _package = package;

        public static async Task RegisterAsync(AsyncPackage package)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            var interceptor = new StartDebugInterceptor(package);
            var register = (IVsRegisterPriorityCommandTarget)await package.GetServiceAsync(typeof(SVsRegisterPriorityCommandTarget));
            if (register == null) return;
            register.RegisterPriorityCommandTarget(0, interceptor, out _);

            var dte = Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(EnvDTE._DTE)) as EnvDTE.DTE;
            if (dte?.Events == null) return;
            interceptor._dteEvents = dte.Events;
            // Unfiltered: CommandEvents[guid,id] never fired for F5 (13:52 log had
            // "hooked" then intercept with no "default cancelled").
            interceptor._cmdAll = interceptor._dteEvents.CommandEvents;
            interceptor._cmdAll.BeforeExecute += interceptor.OnBeforeAnyCommand;
            Log("interceptor registered (CommandEvents hooked)");
        }

        private static bool IsStart(ref Guid group, uint cmdId) =>
            group == VSConstants.GUID_VSStandardCommandSet97 &&
            (cmdId == (uint)VSConstants.VSStd97CmdID.Start ||
             cmdId == (uint)VSConstants.VSStd97CmdID.StartNoDebug);

        public int QueryStatus(ref Guid pguidCmdGroup, uint cCmds, OLECMD[] prgCmds, IntPtr pCmdText)
        {
            if (prgCmds == null || cCmds == 0)
                return (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;

            // Must claim Start here. QueryStatus NOTSUPPORTED lets VS also dispatch
            // Debug.Start to Xbox360Debugger, which has no engine and shows
            // "Unable to start debugging. Check your debugger settings...".
            try
            {
                ThreadHelper.ThrowIfNotOnUIThread();
                var dte = Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(EnvDTE._DTE)) as EnvDTE.DTE;
                if (AlreadyDebugging(dte) || string.IsNullOrEmpty(FirstXbox360ProjectPath(dte)))
                    return (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;
            }
            catch
            {
                return (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;
            }

            bool ours = false;
            for (uint i = 0; i < cCmds; i++)
            {
                if (!IsStart(ref pguidCmdGroup, prgCmds[i].cmdID))
                    continue;
                prgCmds[i].cmdf = (uint)(OLECMDF.OLECMDF_SUPPORTED | OLECMDF.OLECMDF_ENABLED);
                ours = true;
            }
            return ours
                ? VSConstants.S_OK
                : (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;
        }

        private void OnBeforeAnyCommand(string guid, int id, object customIn, object customOut, ref bool cancelDefault)
        {
            if (!Guid.TryParse(guid, out var g) || g != VSConstants.GUID_VSStandardCommandSet97)
                return;
            if (id != (int)VSConstants.VSStd97CmdID.Start &&
                id != (int)VSConstants.VSStd97CmdID.StartNoDebug)
                return;
            Log("CommandEvents Start guid=" + guid + " id=" + id);
            TryCancelDefaultStart(id == (int)VSConstants.VSStd97CmdID.StartNoDebug, ref cancelDefault);
        }

        private void TryCancelDefaultStart(bool noDebug, ref bool cancelDefault)
        {
            // Only Xenia/fallthrough re-issue sets _passthroughStart. _reentry is set
            // for the whole hardware F5 (including MSBuild); skipping cancel then lets
            // the delayed default Debug.Start through and shows the settings dialog.
            if (Volatile.Read(ref _passthroughStart) > 0)
                return;
            string hint = null;
            try
            {
                ThreadHelper.ThrowIfNotOnUIThread();
                var dte = Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(EnvDTE._DTE)) as EnvDTE.DTE;
                if (AlreadyDebugging(dte))
                    return;
                hint = FirstXbox360ProjectPath(dte);
                if (string.IsNullOrEmpty(hint))
                    return;
            }
            catch
            {
                return;
            }

            // Returning S_OK from the priority Exec does not stop VC DebugLaunch
            // (Xbox360Debugger has no engine → "Unable to start debugging...").
            cancelDefault = true;
            Log("Debug.Start default cancelled hint=" + hint);
            QueueLaunch(noDebug, hint);
        }

        public int Exec(ref Guid pguidCmdGroup, uint nCmdID, uint nCmdexecopt, IntPtr pvaIn, IntPtr pvaOut)
        {
            if (!IsStart(ref pguidCmdGroup, nCmdID))
                return (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;
            if (Volatile.Read(ref _reentry) > 0)
                return (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;

            string hint = null;
            try
            {
                ThreadHelper.ThrowIfNotOnUIThread();
                var dte = Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(EnvDTE._DTE)) as EnvDTE.DTE;
                // DAP sessions often leave DTE in design mode. F5/Continue is still
                // Debug.Start — if we swallow it, the adapter never gets continue.
                if (AlreadyDebugging(dte))
                {
                    Log("Debug.Start passthrough (already debugging)");
                    return (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;
                }
                hint = FirstXbox360ProjectPath(dte);
                if (string.IsNullOrEmpty(hint))
                    return (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;
                Log("Debug.Start intercept hint=" + hint);
            }
            catch
            {
                return (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;
            }

            bool noDebug = nCmdID == (uint)VSConstants.VSStd97CmdID.StartNoDebug;
            QueueLaunch(noDebug, hint);
            return VSConstants.S_OK;
        }

        private void QueueLaunch(bool noDebug, string hintVcxproj)
        {
            if (Interlocked.CompareExchange(ref _queued, 1, 0) != 0)
                return;
            string hintCapture = hintVcxproj;
            _package.JoinableTaskFactory
                .RunAsync(async () =>
                {
                    try { await AfterStartAsync(noDebug, hintCapture); }
                    finally { Interlocked.Exchange(ref _queued, 0); }
                })
                .FileAndForget("rxdk360/f5-debug");
        }

        private async Task AfterStartAsync(bool noDebug, string hintVcxproj)
        {
            await Task.Delay(150);
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            var live = Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(EnvDTE._DTE)) as EnvDTE.DTE;
            if (AlreadyDebugging(live))
            {
                Log("AfterStart skipped (already debugging)");
                return;
            }
            Interlocked.Increment(ref _reentry);
            try
            {
                HardwareDebugLauncher.StartupInfo info = null;
                try { info = await HardwareDebugLauncher.GetStartupInfoForF5Async(_package, hintVcxproj); }
                catch { info = null; }

                if (HardwareDebugLauncher.IsXeniaFallthrough(info))
                {
                    await ExecuteDefaultStartAsync(noDebug);
                    return;
                }

                if (HardwareDebugLauncher.IsHardwareF5(info))
                {
                    await HardwareDebugLauncher.LaunchAsync(_package, noDebug, info);
                    return;
                }

                // Xbox 360 that is not Xenia / Emulate DVD: never re-issue Debug.Start.
                // Xbox360Debugger and an empty Local Windows Debugger both produce
                // "Unable to start debugging. Check your debugger settings...".
                if (info != null &&
                    string.Equals(info.PlatformName, "Xbox 360", StringComparison.OrdinalIgnoreCase) &&
                    !string.Equals(info.DeploymentType, HardwareDebugLauncher.EmulateDvd, StringComparison.OrdinalIgnoreCase))
                {
                    await HardwareDebugLauncher.LaunchAsync(_package, noDebug, info);
                    return;
                }

                if (info == null)
                {
                    await HardwareDebugLauncher.ShowMessageAsync(_package,
                        "Could not read the startup project. Set the Xbox 360 title as Startup Project and F5 again.");
                    return;
                }

                await ExecuteDefaultStartAsync(noDebug);
            }
            catch (Exception ex)
            {
                await HardwareDebugLauncher.ShowMessageAsync(_package,
                    "Could not start debugging: " + ex.Message);
            }
            finally
            {
                Interlocked.Decrement(ref _reentry);
            }
        }

        private async Task ExecuteDefaultStartAsync(bool noDebug)
        {
            Interlocked.Increment(ref _passthroughStart);
            try
            {
                var dte = (EnvDTE.DTE)await _package.GetServiceAsync(typeof(EnvDTE.DTE));
                dte?.ExecuteCommand(noDebug ? "Debug.StartWithoutDebugging" : "Debug.Start");
            }
            finally
            {
                Interlocked.Decrement(ref _passthroughStart);
            }
        }

        private static bool AlreadyDebugging(EnvDTE.DTE dte)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            try
            {
                if (dte?.Debugger != null &&
                    dte.Debugger.CurrentMode != EnvDTE.dbgDebugMode.dbgDesignMode)
                    return true;
            }
            catch { }

            try
            {
                var dbg = Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(SVsShellDebugger)) as IVsDebugger;
                if (dbg != null)
                {
                    var mode = new DBGMODE[1];
                    if (dbg.GetMode(mode) == VSConstants.S_OK)
                    {
                        DBGMODE stripped = mode[0] & ~DBGMODE.DBGMODE_EncMask;
                        if (stripped != DBGMODE.DBGMODE_Design)
                            return true;
                    }
                }
            }
            catch { }

            try
            {
                var mon = Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(SVsShellMonitorSelection)) as IVsMonitorSelection;
                Guid ctx = new Guid("ADFC4E61-0397-11D1-9F4E-00A0C911004F"); // UICONTEXT_Debugging
                if (mon != null &&
                    mon.GetCmdUIContextCookie(ref ctx, out uint cookie) == VSConstants.S_OK &&
                    mon.IsCmdUIContextActive(cookie, out int active) == VSConstants.S_OK &&
                    active != 0)
                    return true;
            }
            catch { }

            return false;
        }

        private static void Log(string message)
        {
            try
            {
                File.AppendAllText(
                    Path.Combine(Path.GetTempPath(), "rxdk360-f5.log"),
                    DateTime.Now.ToString("HH:mm:ss.fff") + " " + message + Environment.NewLine);
            }
            catch { }
        }

        private static string FirstXbox360ProjectPath(EnvDTE.DTE dte)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (dte?.Solution?.Projects == null) return null;
            foreach (EnvDTE.Project p in dte.Solution.Projects)
            {
                string path = FirstXbox360PathInTree(p);
                if (!string.IsNullOrEmpty(path)) return path;
            }
            return null;
        }

        private static string FirstXbox360PathInTree(EnvDTE.Project p)
        {
            try
            {
                string fn = p.FullName;
                if (!string.IsNullOrEmpty(fn) &&
                    fn.EndsWith(".vcxproj", StringComparison.OrdinalIgnoreCase) &&
                    FileLooksLikeXbox360(fn))
                    return fn;
                if (p.ProjectItems == null) return null;
                foreach (EnvDTE.ProjectItem item in p.ProjectItems)
                {
                    var sub = item.SubProject;
                    if (sub == null) continue;
                    string nested = FirstXbox360PathInTree(sub);
                    if (!string.IsNullOrEmpty(nested)) return nested;
                }
            }
            catch { }
            return null;
        }

        private static bool FileLooksLikeXbox360(string vcxproj)
        {
            try
            {
                string text = File.ReadAllText(vcxproj);
                return text.IndexOf("|Xbox 360", StringComparison.OrdinalIgnoreCase) >= 0
                    || text.IndexOf("'Xbox 360'", StringComparison.OrdinalIgnoreCase) >= 0;
            }
            catch { return false; }
        }
    }
}
