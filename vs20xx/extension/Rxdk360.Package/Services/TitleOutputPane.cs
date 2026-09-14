// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.IO;
using System.Text;
using System.Threading;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Task = System.Threading.Tasks.Task;

namespace Rxdk360.Package.Services
{
    /// <summary>
    /// Same split as RXDK-VS20XX: title DbgPrint / DM_DEBUGSTR goes to an "Xbox Title"
    /// pane. Adapter and XBDM protocol stay in the Debug pane via DAP console output.
    /// The adapter appends title text to <c>__titleOutputFile</c>; this tails it.
    /// </summary>
    internal sealed class TitleOutputPane
    {
        private static readonly Guid PaneGuid = new Guid("8e3c1a6b-4d2f-4c9a-9b71-360d7a1c5e8f");
        private const string PaneTitle = "Xbox Title";

        private readonly AsyncPackage _package;
        private IVsOutputWindowPane _pane;
        private Timer _timer;
        private string _file = "";
        private long _offset;
        private bool _revealed;
        private EnvDTE.DebuggerEvents _debuggerEvents;
        private EnvDTE._dispDebuggerEvents_OnEnterDesignModeEventHandler _onEnterDesignMode;

        public TitleOutputPane(AsyncPackage package) => _package = package;

        public async Task StartAsync(string file)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            _file = file;
            _offset = 0;
            _revealed = false;
            try { File.WriteAllText(file, string.Empty); } catch { }

            if (await _package.GetServiceAsync(typeof(SVsOutputWindow)) is IVsOutputWindow ow)
            {
                var guid = PaneGuid;
                ow.CreatePane(ref guid, PaneTitle, fInitVisible: 1, fClearWithSolution: 0);
                ow.GetPane(ref guid, out _pane);
                _pane?.Clear();
                _pane?.OutputStringThreadSafe("--- Xbox title output ---" + Environment.NewLine);
            }

            if (Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(EnvDTE._DTE)) is EnvDTE.DTE dte)
            {
                _debuggerEvents = dte.Events?.DebuggerEvents;
                if (_debuggerEvents != null)
                {
                    _onEnterDesignMode = _ => Stop();
                    _debuggerEvents.OnEnterDesignMode += _onEnterDesignMode;
                }
            }

            _timer = new Timer(_ => Poll(), null, 0, 100);
        }

        private void Poll()
        {
            try
            {
                if (string.IsNullOrEmpty(_file) || !File.Exists(_file))
                    return;
                var len = new FileInfo(_file).Length;
                if (len <= _offset)
                    return;

                byte[] buf;
                using (var fs = new FileStream(_file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                {
                    fs.Seek(_offset, SeekOrigin.Begin);
                    buf = new byte[len - _offset];
                    var read = fs.Read(buf, 0, buf.Length);
                    if (read < buf.Length)
                        Array.Resize(ref buf, read);
                }
                _offset += buf.Length;

                var text = Encoding.UTF8.GetString(buf);
                if (text.Length == 0)
                    return;

                text = text.Replace("\r\n", "\n").Replace("\n", Environment.NewLine);
                var pane = _pane;
#pragma warning disable VSTHRD010
                pane?.OutputStringThreadSafe(text);
#pragma warning restore VSTHRD010
                if (!_revealed && pane != null)
                {
                    _revealed = true;
                    _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
                    {
                        await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                        pane.Activate();
                    });
                }
            }
            catch { }
        }

        public void Stop()
        {
            var timer = _timer;
            _timer = null;
            Poll();
            try { timer?.Dispose(); } catch { }

            var handler = _onEnterDesignMode;
            var events = _debuggerEvents;
            if (handler != null && events != null)
            {
                _onEnterDesignMode = null;
                _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
                {
                    await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                    try { events.OnEnterDesignMode -= handler; } catch { }
                });
            }
        }
    }
}
