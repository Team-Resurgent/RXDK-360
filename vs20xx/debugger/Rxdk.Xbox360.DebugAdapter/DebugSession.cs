// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
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
        private uint _stoppedThread;
        private XContext _ctx;
        private bool _haveCtx;
        private const int LocalsRef = 1000;
        private readonly Dictionary<uint, (ulong addr, int line)> _breakpoints = new();

        public bool Terminated { get; private set; }

        public DebugSession(DapConnection dap) { _dap = dap; }

        public void Handle(DapConnection.Message req)
        {
            try
            {
                switch (req.Command)
                {
                    case "initialize": Initialize(req); break;
                    case "launch": Launch(req); break;
                    case "setBreakpoints": SetBreakpoints(req); break;
                    case "configurationDone": _dap.SendResponse(req, true); break;
                    case "threads": Threads(req); break;
                    case "stackTrace": StackTrace(req); break;
                    case "scopes": Scopes(req); break;
                    case "variables": Variables(req); break;
                    case "continue": Continue(req); break;
                    case "next": case "stepIn": case "stepOut": Step(req); break;
                    case "pause": _kit?.Stop(); _dap.SendResponse(req, true); break;
                    case "disconnect": Disconnect(req); break;
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
                ["supportsFunctionBreakpoints"] = false,
                ["supportsEvaluateForHovers"] = false,
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
            string console = Str(a, "console");                        // devkit name/ip
            bool stopAtEntry = Bool(a, "stopAtEntry", true);

            if (symbols.Length == 0 || !File.Exists(symbols))
                throw new FileNotFoundException($"symbol file not found: {symbols}");
            _sym = Symbols.FromElf(symbols);

            if (console.Length != 0)
            {
                _kit = new XbdmClient();
                _kit.Notification += OnKitEvent;
                _kit.Connect(console);
                _kit.AttachDebugger();
                _kit.ListenForEvents();
                _remoteImage = "devkit:\\" + Path.GetFileName(program);
                _kit.Mkdir("devkit:\\");
                _kit.SendFile(program, _remoteImage);
                _kit.LaunchTitle(_remoteImage, stopAtLoad: stopAtEntry);
                _dap.SendEvent("output", new { category = "console", output = $"deployed {program} -> {_remoteImage}\n" });
            }
            _dap.SendResponse(req, true);
        }

        private void SetBreakpoints(DapConnection.Message req)
        {
            var a = req.Arguments;
            string path = a.TryGetProperty("source", out var src) && src.TryGetProperty("path", out var p)
                ? p.GetString() ?? "" : "";
            var results = new List<object>();
            if (a.TryGetProperty("breakpoints", out var bps))
            {
                foreach (var bp in bps.EnumerateArray())
                {
                    int line = bp.TryGetProperty("line", out var l) ? l.GetInt32() : 0;
                    var r = _sym?.ResolveBreakpoint(Path.GetFileName(path), line);
                    if (r != null)
                    {
                        uint addr = (uint)r.Value.address;
                        _breakpoints[addr] = (addr, r.Value.line);
                        try { _kit?.SetBreakpoint(addr); } catch { }
                        results.Add(new { verified = true, line = r.Value.line, message = $"0x{addr:X8}" });
                    }
                    else results.Add(new { verified = false, line });
                }
            }
            _dap.SendResponse(req, true, new { breakpoints = results });
        }

        private void Threads(DapConnection.Message req)
        {
            var list = new List<object>();
            if (_kit != null)
                foreach (var t in _kit.GetThreads()) list.Add(new { id = (int)t, name = $"thread 0x{t:X8}" });
            if (list.Count == 0) list.Add(new { id = 1, name = "main" });
            _dap.SendResponse(req, true, new { threads = list });
        }

        private void StackTrace(DapConnection.Message req)
        {
            var frames = new List<object>();
            if (_haveCtx && _sym != null)
            {
                ulong pc = _ctx.Iar;
                var fn = _sym.FunctionAt(pc);
                var ln = _sym.LineAt(pc);
                frames.Add(new
                {
                    id = 0,
                    name = fn?.Name ?? $"0x{pc:X8}",
                    line = ln?.Line ?? 0,
                    column = 0,
                    source = ln != null ? new { name = Path.GetFileName(ln.File), path = ln.File } : null,
                });
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
            var vars = new List<object>();
            if (_haveCtx && _sym != null)
            {
                var fn = _sym.FunctionAt(_ctx.Iar);
                if (fn != null)
                    foreach (var s in _sym.Locals(fn, _ctx))
                        vars.Add(new { name = s.Name, value = ReadValue(s), type = s.TypeName, variablesReference = 0 });
            }
            _dap.SendResponse(req, true, new { variables = vars });
        }

        private string ReadValue(VarSlot s)
        {
            try
            {
                ulong raw;
                if (s.Register is int reg) raw = _ctx.Gpr[reg];
                else if (s.Address is uint addr && _kit != null)
                {
                    var b = _kit.ReadMemory(addr, s.Size);
                    raw = 0; foreach (var x in b) raw = (raw << 8) | x;      // big-endian
                }
                else return "<optimized out>";
                return Format(s.TypeName, raw, s.Size);
            }
            catch { return "<unavailable>"; }
        }

        private static string Format(string type, ulong raw, int size)
        {
            string t = type.Replace("const ", "").Trim();
            if (t.EndsWith("*")) return $"0x{raw:X8}";
            if (t is "char" or "signed char" or "unsigned char")
            {
                char ch = (char)(raw & 0xff);
                return char.IsControl(ch) ? ((long)raw).ToString() : $"'{ch}' ({(long)raw})";
            }
            if (t is "_Bool" or "bool") return raw != 0 ? "true" : "false";
            bool unsigned = t.Contains("unsigned");
            if (!unsigned)
            {
                long sv = size switch
                {
                    1 => (sbyte)raw,
                    2 => (short)raw,
                    4 => (int)raw,
                    _ => (long)raw,
                };
                return sv.ToString();
            }
            return raw.ToString();
        }

        private void Continue(DapConnection.Message req)
        {
            _haveCtx = false;
            try { _kit?.Go(); } catch { }
            _dap.SendResponse(req, true, new { allThreadsContinued = true });
        }

        private void Step(DapConnection.Message req)
        {
            // Line step: set temporary breakpoints at the following line rows in the
            // current function (and the return address), then run. A break clears them.
            if (_haveCtx && _sym != null && _kit != null)
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
            try { _kit?.Go(); } catch { }
            _dap.SendResponse(req, true);
        }

        private void OnKitEvent(XbdmNotification n)
        {
            // A halt (breakpoint/exception/initial) - capture the stopped frame and
            // tell the editor. Thread id comes from the event when present.
            uint tid = n.Thread ?? (_kit != null && _kit.GetThreads() is { Length: > 0 } ts ? ts[0] : 1);
            _stoppedThread = tid;
            try { _ctx = _kit!.GetContext(tid, XContextFlags.Control | XContextFlags.Integer); _haveCtx = true; } catch { _haveCtx = false; }
            string reason = n.Kind switch
            {
                "break" or "data" => "breakpoint",
                "exception" or "assert" => "exception",
                _ => "step",
            };
            _dap.SendEvent("stopped", new { reason, threadId = (int)tid, allThreadsStopped = true });
        }

        private void Disconnect(DapConnection.Message req)
        {
            try { _kit?.Go(); } catch { }
            _kit?.Dispose();
            _kit = null;
            Terminated = true;
            _dap.SendResponse(req, true);
            _dap.SendEvent("terminated");
        }

        // ---- small JSON helpers ----
        private static string Str(JsonElement e, string name) =>
            e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String
                ? v.GetString() ?? "" : "";
        private static bool Bool(JsonElement e, string name, bool dflt) =>
            e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) &&
            (v.ValueKind == JsonValueKind.True || v.ValueKind == JsonValueKind.False) ? v.GetBoolean() : dflt;
    }
}
