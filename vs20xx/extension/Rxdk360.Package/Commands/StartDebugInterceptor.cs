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

        private StartDebugInterceptor(AsyncPackage package) => _package = package;

        public static async Task RegisterAsync(AsyncPackage package)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            var register = (IVsRegisterPriorityCommandTarget)await package.GetServiceAsync(typeof(SVsRegisterPriorityCommandTarget));
            if (register == null) return;
            register.RegisterPriorityCommandTarget(0, new StartDebugInterceptor(package), out _);
        }

        private static bool IsStart(ref Guid group, uint cmdId) =>
            group == VSConstants.GUID_VSStandardCommandSet97 &&
            (cmdId == (uint)VSConstants.VSStd97CmdID.Start ||
             cmdId == (uint)VSConstants.VSStd97CmdID.StartNoDebug);

        public int QueryStatus(ref Guid pguidCmdGroup, uint cCmds, OLECMD[] prgCmds, IntPtr pCmdText) =>
            (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;

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
            string hintCapture = hint;
            _package.JoinableTaskFactory
                .RunAsync(() => AfterStartAsync(noDebug, hintCapture))
                .FileAndForget("rxdk360/f5-debug");
            return VSConstants.S_OK;
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
                    var dte = (EnvDTE.DTE)await _package.GetServiceAsync(typeof(EnvDTE.DTE));
                    dte?.ExecuteCommand(noDebug ? "Debug.StartWithoutDebugging" : "Debug.Start");
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

                var dte2 = (EnvDTE.DTE)await _package.GetServiceAsync(typeof(EnvDTE.DTE));
                dte2?.ExecuteCommand(noDebug ? "Debug.StartWithoutDebugging" : "Debug.Start");
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
