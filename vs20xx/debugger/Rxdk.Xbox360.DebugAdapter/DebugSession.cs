// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Threading;
using Rxdk.Xbox360.Dwarf;
using Rxdk.Xbox360.Xbdm;

namespace Rxdk.Xbox360.DebugAdapter
{
    /// <summary>
    /// The RXDK-360 devkit debug adapter: it answers Debug Adapter Protocol requests
    /// from the editor by resolving symbols with <see cref="Symbols"/> (the title's
    /// DWARF) and driving the console with <see cref="XbdmClient"/> (xbdm.dll). This
    /// is the glue that makes F5-to-devkit step source: reboot the title stopped,
    /// set breakpoints at source lines, and on a break map the PC back to source and
    /// read locals from the stopped frame.
    /// </summary>
    public sealed class DebugSession
    {
        private readonly DapConnection _dap;
        private Symbols? _sym;
        private XbdmClient? _kit;
        private string _remoteImage = "";
        private string _titleOutputFile = "";
        private uint _stoppedThread;
        private XContext _ctx;
        private bool _haveCtx;
        private bool _holdingInitialBreak;
        private int _paused; // 1 = already reported stopped; ignore extra kit breaks
        private uint _titleBase, _titleSize;
        private const int LocalsRef = 1000;
        private int _nextChildRef = 1001;
        private readonly Dictionary<int, string> _expand = new();
        private readonly List<(string path, uint addr, int line)> _breakpoints = new();

        public bool Terminated { get; private set; }

        public DebugSession(DapConnection dap)
        {
            _dap = dap;
            _dap.ProtocolTrace = (dir, json) =>
                _dap.SendEvent("output", new { category = "console", output = dir + " " + json + "\n" });
        }

        public void Handle(DapConnection.Message req)
        {
            try
            {
                switch (req.Command)
                {
                    case "initialize": Initialize(req); break;
                    case "launch": Launch(req); break;
                    case "setBreakpoints": SetBreakpoints(req); break;
                    case "configurationDone": ConfigurationDone(req); break;
                    case "threads": Threads(req); break;
                    case "stackTrace": StackTrace(req); break;
                    case "scopes": Scopes(req); break;
                    case "variables": Variables(req); break;
                    case "evaluate": Evaluate(req); break;
                    case "continue": Continue(req); break;
                    case "next": case "stepIn": case "stepOut": Step(req); break;
                    case "pause": Pause(req); break;
                    case "disconnect": Disconnect(req, terminateDefault: true); break;
                    case "terminate": Disconnect(req, terminateDefault: true); break;
                    default: _dap.SendResponse(req, true); break;      // politely accept unknowns
                }
            }
            catch (Exception e)
            {
                _dap.SendResponse(req, false, message: e.Message);
            }
        }

        private void Initialize(DapConnection.Message req)
        {
            _dap.SendResponse(req, true, new Dictionary<string, object?>
            {
                ["supportsConfigurationDoneRequest"] = true,
                ["supportsTerminateRequest"] = true,
                ["supportsFunctionBreakpoints"] = false,
                ["supportsEvaluateForHovers"] = true,
                ["supportsStepBack"] = false,
            });
            _dap.SendEvent("initialized");
        }

        private void Launch(DapConnection.Message req)
        {
            var a = req.Arguments;
            string program = Str(a, "program");                        // the .xex
            string symbols = Str(a, "symbols");                        // the .elf (default: program.elf)
            if (symbols.Length == 0 && program.Length != 0)
                symbols = Path.ChangeExtension(program, ".elf");
            string console = Str(a, "console");
            if (console.Length == 0)
                console = XbdmClient.DefaultConsole() ?? "";
            bool stopAtEntry = Bool(a, "stopAtEntry", true) && !Bool(a, "noDebug", false);
            string remoteDir = Str(a, "remoteDir");
            if (remoteDir.Length == 0)
                remoteDir = "devkit:\\" + Path.GetFileNameWithoutExtension(program);
            string? xbdm = XbdmClient.LocateXbdm();
            _titleOutputFile = Str(a, "__titleOutputFile");
            _dap.SendEvent("output", new { category = "console", output =
                (xbdm != null ? $"xbdm {xbdm}\n" : "xbdm.dll NOT found (install RXDK-360)\n") });
            if (!stopAtEntry)
            {
                _kit = new XbdmClient { ResumeOnDispose = false };
                _kit.Connect(console);
                string remote = _kit.DeployTitle(program, remoteDir);
                _kit.SetTitle(remoteDir.EndsWith("\\") ? remoteDir : remoteDir + "\\", Path.GetFileName(program));
                _kit.LaunchTitle(remote, stopAtLoad: false);
                _remoteImage = remote;
                _dap.SendEvent("output", new { category = "console", output = $"running {remote}\n" });
                _dap.SendResponse(req, true);
                return;
            }

            _sym = Symbols.Open(program, symbols.Length != 0 ? symbols : null);
            if (_sym != null)
                _dap.SendEvent("output", new { category = "console", output = $"symbols {_sym.Kind} {_sym.Path}\n" });
            else if (symbols.Length != 0)
                _dap.SendEvent("output", new { category = "console", output = $"no symbols next to {program}\n" });

            if (console.Length == 0)
                throw new InvalidOperationException("no console: set launch.console or XenonSDK\\XboxName ($(DefaultConsole)).");

            _kit = new XbdmClient { ResumeOnDispose = false };
            _kit.Notification += OnKitEvent;
            _kit.Connect(console);
            _holdingInitialBreak = true;
            var stop = _kit.LaunchStopped(program, remoteDir: remoteDir);
            _remoteImage = "devkit:\\" + Path.GetFileNameWithoutExtension(program) + "\\" + Path.GetFileName(program);
            string titleHint = Path.GetFileNameWithoutExtension(program);
            _kit.TryFindModule(titleHint, out _titleBase, out _titleSize, out _);
            _dap.SendEvent("output", new { category = "console", output = $"launched {_remoteImage} initial={stop} base=0x{_titleBase:X8}\n" });
            _dap.SendResponse(req, true);
        }

        /// <summary>Official VS: after the initial wait, setBreakpoints has run; auto-Go.
        /// The next break (a source BP) is what the user sees. Thread-create START/STOP
        /// while sitting on that BP is not Continue.</summary>
        private void ConfigurationDone(DapConnection.Message req)
        {
            _dap.SendResponse(req, true);
            _holdingInitialBreak = false;
            Interlocked.Exchange(ref _paused, 0);
            try { _kit?.ContinueAll(); } catch { }
        }

        private void SetBreakpoints(DapConnection.Message req)
        {
            var a = req.Arguments;
            string path = a.TryGetProperty("source", out var src) && src.TryGetProperty("path", out var p)
                ? p.GetString() ?? "" : "";
            var results = new List<object>();
            var wanted = new HashSet<uint>();
            var wantedLines = new Dictionary<uint, int>();
            if (a.TryGetProperty("breakpoints", out var bps) && bps.ValueKind == JsonValueKind.Array)
            {
                foreach (var bp in bps.EnumerateArray())
                {
                    int line = bp.TryGetProperty("line", out var l) ? l.GetInt32() : 0;
                    var r = _sym?.ResolveBreakpoint(Path.GetFileName(path), line);
                    if (r != null)
                    {
                        uint addr = TitleAddress.ExeToXex((uint)r.Value.address,
                            _titleBase != 0 ? _titleBase : TitleAddress.DefaultBase);
                        wanted.Add(addr);
                        wantedLines[addr] = r.Value.line;
                        results.Add(new { verified = true, line = r.Value.line, message = $"0x{addr:X8}" });
                    }
                    else results.Add(new { verified = false, line });
                }
            }

            // DAP setBreakpoints replaces every breakpoint for this source. An empty
            // list is how VS clears the file; without RemoveBreakpoint the kit keeps trapping.
            for (int i = _breakpoints.Count - 1; i >= 0; i--)
            {
                var e = _breakpoints[i];
                if (!string.Equals(e.path, path, StringComparison.OrdinalIgnoreCase))
                    continue;
                if (wanted.Contains(e.addr))
                    continue;
                try { _kit?.RemoveBreakpoint(e.addr); } catch { }
                _breakpoints.RemoveAt(i);
            }

            foreach (uint addr in wanted)
            {
                if (_breakpoints.Exists(e => e.addr == addr &&
                    string.Equals(e.path, path, StringComparison.OrdinalIgnoreCase)))
                    continue;
                try { _kit?.SetBreakpoint(addr); } catch { }
                _breakpoints.Add((path, addr, wantedLines[addr]));
            }

            _dap.SendResponse(req, true, new { breakpoints = results });
        }

        private void Threads(DapConnection.Message req)
        {
            EnsureContext();
            var list = new List<object>();
            var seen = new HashSet<int>();
            void Add(uint t)
            {
                int id = DapTid(t);
                if (id <= 0 || !seen.Add(id)) return;
                list.Add(new Dictionary<string, object?> { ["id"] = id, ["name"] = $"thread 0x{t:X8}" });
            }
            if (_stoppedThread != 0) Add(_stoppedThread);
            try
            {
                if (_kit != null)
                    foreach (var t in _kit.GetThreads()) Add(t);
            }
            catch { }
            if (list.Count == 0) list.Add(new Dictionary<string, object?> { ["id"] = 1, ["name"] = "main" });
            _dap.SendResponse(req, true, new { threads = list });
        }

        private void StackTrace(DapConnection.Message req)
        {
            EnsureContext();
            var frames = new List<object>();
            if (_haveCtx)
            {
                uint pc = _ctx.Iar;
                var loc = _sym?.LineAtKit(pc, _titleBase);
                string file = loc?.File ?? "";
                string fn = loc is { Function: { Length: > 0 } } ? loc.Value.Function : $"0x{pc:X8}";
                string srcName = file.Length == 0 ? "unknown" : Path.GetFileName(file);
                var frame = new Dictionary<string, object?>
                {
                    ["id"] = 0,
                    ["name"] = fn,
                    ["line"] = loc?.Line ?? 0,
                    ["column"] = 1,
                    ["source"] = new Dictionary<string, object?>
                    {
                        ["name"] = srcName,
                        ["path"] = file,
                    },
                };
                if (file.Length == 0)
                    ((Dictionary<string, object?>)frame["source"]!)["presentationHint"] = "deemphasize";
                frames.Add(frame);
            }
            _dap.SendResponse(req, true, new { stackFrames = frames, totalFrames = frames.Count });
        }

        private void Scopes(DapConnection.Message req)
        {
            _dap.SendResponse(req, true, new
            {
                scopes = new[] { new { name = "Locals", variablesReference = LocalsRef, expensive = false } }
            });
        }

        private void Variables(DapConnection.Message req)
        {
            EnsureContext();
            var vars = new List<object>();
            int varRef = Int(req.Arguments, "variablesReference", LocalsRef);
            var list = new ValueList();
            if (varRef != LocalsRef && _expand.TryGetValue(varRef, out var expandKey))
            {
                if (TryPdbValues(out var pdbVals, out var ctx, out var memory))
                    pdbVals.TryEmitMembers(expandKey, ref ctx, list, memory);
                else if (TryDwarfValues(out var dwarfVals, out var dwarfMem))
                    dwarfVals.TryEmitMembers(expandKey, list, dwarfMem);
            }
            else if (TryPdbValues(out var pdbLocals, out var locCtx, out var locMem))
            {
                pdbLocals.EmitLocals(ref locCtx, list, locMem);
            }
            else if (TryDwarfValues(out var dwarfLocals, out var dwarfLocMem))
            {
                dwarfLocals.EmitLocals(list, dwarfLocMem);
            }

            foreach (var row in list.Rows)
                vars.Add(DapVar(row));
            _dap.SendResponse(req, true, new { variables = vars });
        }

        /// <summary>
        /// Autos / Watch / hover all send <c>evaluate</c>. Hover fails quietly when the
        /// token is not a live value (same as RXDK-VS20XX) so VS does not show an error
        /// tip. A successful hover/Autos result includes the formatted value and, for
        /// structs, a variablesReference so the data tip can expand.
        /// </summary>
        private void Evaluate(DapConnection.Message req)
        {
            string context = Str(req.Arguments, "context");
            string expression = Str(req.Arguments, "expression");
            bool isHover = string.Equals(context, "hover", StringComparison.OrdinalIgnoreCase);
            string? error = null;

            EnsureContext();
            if (expression.Length > 0 && TryPdbValues(out var pdb, out var ctx, out var memory)
                && pdb.TryEvaluate(expression, ref ctx, memory, out var value, out error, out var expandable))
            {
                int child = 0;
                if (expandable)
                {
                    child = _nextChildRef++;
                    _expand[child] = expression;
                }
                _dap.SendResponse(req, true, new Dictionary<string, object?>
                {
                    ["result"] = value,
                    ["variablesReference"] = child,
                });
                return;
            }

            if (expression.Length > 0 && TryDwarfValues(out var dwarf, out var dwarfMem)
                && dwarf.TryEvaluate(expression, dwarfMem, out var dwarfValue, out var dwarfExpand, out var dwarfKey))
            {
                int child = 0;
                if (dwarfExpand && dwarfKey.Length > 0)
                {
                    child = _nextChildRef++;
                    _expand[child] = dwarfKey;
                }
                _dap.SendResponse(req, true, new Dictionary<string, object?>
                {
                    ["result"] = dwarfValue,
                    ["variablesReference"] = child,
                });
                return;
            }

            if (isHover)
            {
                _dap.SendResponse(req, false, message: error ?? "not available");
                return;
            }

            _dap.SendResponse(req, true, new Dictionary<string, object?>
            {
                ["result"] = error is { Length: > 0 } ? error : "<not available>",
                ["variablesReference"] = 0,
            });
        }

        private bool TryPdbValues(out ManagedValues values, out XContext ctx, out KitMemory memory)
        {
            values = null!;
            ctx = _ctx;
            memory = null!;
            var pdb = _sym?.Pdb;
            if (!_haveCtx || _kit == null || pdb == null || _ctx.Gpr == null)
                return false;
            uint baseAddr = _titleBase != 0 ? _titleBase : TitleAddress.DefaultBase;
            values = new ManagedValues(pdb, baseAddr);
            memory = new KitMemory(_kit);
            return true;
        }

        private bool TryDwarfValues(out DwarfValues values, out KitMemory memory)
        {
            values = null!;
            memory = null!;
            if (!_haveCtx || _kit == null || _sym?.Info == null || _ctx.Gpr == null)
                return false;
            values = new DwarfValues(_sym, _ctx);
            memory = new KitMemory(_kit);
            return true;
        }

        private object DapVar(ValueRow row)
        {
            int child = 0;
            if (row.Expandable && row.ExpandKey.Length > 0)
            {
                child = _nextChildRef++;
                _expand[child] = row.ExpandKey;
            }
            return new Dictionary<string, object?>
            {
                ["name"] = row.Name,
                ["value"] = row.Value,
                ["type"] = row.Type,
                ["variablesReference"] = child,
            };
        }

        private void Continue(DapConnection.Message req)
        {
            // Reply first. ContinueAll can hit the next source BP on the notify thread
            // before this returns; VS then treats a later continue-success as "running"
            // and the Stop/break UI dies (seen as stopped seq after continue seq).
            _haveCtx = false;
            _dap.SendResponse(req, true, new { allThreadsContinued = true });
            Interlocked.Exchange(ref _paused, 0);
            try { _kit?.ContinueAll(); } catch { }
        }

        private void Step(DapConnection.Message req)
        {
            // Line step: set temporary breakpoints at the following line rows in the
            // current function (and the return address), then run. A break clears them.
            if (_haveCtx && _sym != null && _sym.Info != null && _kit != null)
            {
                var fn = _sym.FunctionAt(_ctx.Iar);
                var cur = _sym.LineAt(_ctx.Iar);
                if (fn != null)
                {
                    foreach (var u in _sym.Info.Units)
                        foreach (var r in u.Lines)
                            if (!r.EndSequence && r.Address > _ctx.Iar && r.Address >= fn.LowPc && r.Address < fn.HighPc
                                && (cur == null || r.Line != cur.Line))
                                try { _kit.SetBreakpoint((uint)r.Address); } catch { }
                    try { _kit.SetBreakpoint((uint)_ctx.Lr); } catch { }        // return
                }
            }
            _haveCtx = false;
            _dap.SendResponse(req, true);
            Interlocked.Exchange(ref _paused, 0);
            try { _kit?.ContinueAll(); } catch { }
        }

        private void Pause(DapConnection.Message req)
        {
            try { _kit?.Stop(); } catch { }
            _dap.SendResponse(req, true);
            if (Interlocked.CompareExchange(ref _paused, 1, 0) == 0)
            {
                _haveCtx = false;
                _dap.SendEvent("stopped", new Dictionary<string, object?>
                {
                    ["reason"] = "pause",
                    ["threadId"] = DapTid(_stoppedThread != 0 ? _stoppedThread : 1),
                    ["allThreadsStopped"] = true,
                });
            }
        }

        /// <summary>
        /// XBDM fires this on its notify thread. Do not call back into xbdm.dll here
        /// (GetContext / GetThreads) — that races the DAP thread and Visual Studio then
        /// surfaces a delayed NullReferenceException. Context is fetched on the next
        /// threads / stackTrace request instead. Extra break notifications while already
        /// stopped are ignored so VS is not sent a second stopped event with a null source.
        /// </summary>
        private void OnKitEvent(XbdmNotification n)
        {
            try
            {
                if (n.Kind is "debugstr" or "assert")
                {
                    string text = n.Fields.TryGetValue("string", out var s) ? s : n.Raw;
                    if (!string.IsNullOrEmpty(text))
                        WriteTitleOutput(text.EndsWith("\n", StringComparison.Ordinal) ? text : text + "\n");
                    return;
                }
                if (_holdingInitialBreak) return;
                if (n.Kind is not ("break" or "singlestep" or "data" or "exception")) return;
                if (Interlocked.CompareExchange(ref _paused, 1, 0) != 0) return;

                uint tid = n.Thread ?? 1;
                if (tid == 0) tid = 1;
                _stoppedThread = tid;
                _haveCtx = false;
                _expand.Clear();
                _nextChildRef = LocalsRef + 1;
                _dap.SendEvent("output", new { category = "console", output = "KIT " + n + "\n" });
                string reason = n.Kind switch
                {
                    "break" or "data" => "breakpoint",
                    "exception" or "assert" => "exception",
                    _ => "step",
                };
                _dap.SendEvent("stopped", new Dictionary<string, object?>
                {
                    ["reason"] = reason,
                    ["threadId"] = DapTid(tid),
                    ["allThreadsStopped"] = true,
                });
            }
            catch { }
        }

        private void WriteTitleOutput(string text)
        {
            if (string.IsNullOrEmpty(_titleOutputFile)) return;
            try { File.AppendAllText(_titleOutputFile, text); } catch { }
        }

        private void Disconnect(DapConnection.Message req, bool terminateDefault)
        {
            bool terminate = !req.HasArguments || Bool(req.Arguments, "terminateDebuggee", terminateDefault);
            TearDownKit(terminate);
            Terminated = true;
            _dap.SendResponse(req, true);
            _dap.SendEvent("terminated");
        }

        private void TearDownKit(bool terminate)
        {
            var kit = _kit;
            _kit = null;
            if (kit == null) return;
            try { kit.Notification -= OnKitEvent; } catch { }
            try
            {
                if (terminate) kit.RebootToDashboard();
                else kit.Resume(detach: true);
            }
            catch { }
            try { kit.Dispose(); } catch { }
        }

        private void EnsureContext()
        {
            if (_haveCtx || _kit == null) return;
            try
            {
                uint tid = _stoppedThread;
                if (tid == 0)
                {
                    var ts = _kit.GetThreads();
                    tid = ts.Length > 0 ? ts[0] : 0;
                }
                if (tid == 0) return;
                _stoppedThread = tid;
                _ctx = _kit.GetContext(tid, XContextFlags.Control | XContextFlags.Integer);
                _haveCtx = _ctx.Gpr != null;
            }
            catch { _haveCtx = false; }
        }

        private static int DapTid(uint tid)
        {
            if (tid == 0) return 1;
            return tid <= int.MaxValue ? (int)tid : unchecked((int)(tid & 0x7FFFFFFF));
        }

        // ---- small JSON helpers ----
        private static string Str(JsonElement e, string name) =>
            e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String
                ? v.GetString() ?? "" : "";
        private static bool Bool(JsonElement e, string name, bool dflt) =>
            e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) &&
            (v.ValueKind == JsonValueKind.True || v.ValueKind == JsonValueKind.False) ? v.GetBoolean() : dflt;
        private static int Int(JsonElement e, string name, int dflt)
        {
            if (e.ValueKind != JsonValueKind.Object || !e.TryGetProperty(name, out var v))
                return dflt;
            if (v.ValueKind == JsonValueKind.Number && v.TryGetInt32(out var n))
                return n;
            return dflt;
        }
    }
}
