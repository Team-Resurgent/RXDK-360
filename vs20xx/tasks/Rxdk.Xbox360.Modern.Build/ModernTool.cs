// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Rxdk.Xbox360.Modern.Build
{
    /// <summary>
    /// Shared helpers for the modern (clang/LLVM) Xbox 360 tasks: running a tool
    /// and capturing its output, and turning clang/lld diagnostics into MSBuild
    /// errors/warnings so they surface in the Visual Studio Error List.
    /// </summary>
    public abstract class ModernTool : Task
    {
        /// <summary>Result of running a child process.</summary>
        protected sealed class ProcResult
        {
            public int ExitCode;
            public string StdOut = "";
            public string StdErr = "";
            public string Combined => StdOut + StdErr;
        }

        // clang / ld.lld GNU-style diagnostics, e.g.
        //   main.c:12:5: error: use of undeclared identifier 'foo'
        //   ld.lld: error: undefined symbol: DbgPrint
        private static readonly Regex FileDiag =
            new Regex(@"^(?<file>[^:]+(?::[^:]+)?):(?<line>\d+):(?<col>\d+):\s*(?<sev>error|warning|note):\s*(?<msg>.*)$",
                      RegexOptions.Compiled);
        private static readonly Regex ToolDiag =
            new Regex(@"^(?<tool>[\w.\-]+):\s*(?<sev>error|warning):\s*(?<msg>.*)$",
                      RegexOptions.Compiled);

        /// <summary>Run <paramref name="exe"/> with <paramref name="args"/>, capturing output.</summary>
        protected ProcResult Run(string exe, IEnumerable<string> args, string workingDir = null)
        {
            var psi = new ProcessStartInfo
            {
                FileName = exe,
                Arguments = JoinArgs(args),
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                WorkingDirectory = workingDir ?? Environment.CurrentDirectory,
            };
            Log.LogMessage(MessageImportance.Low, "  " + exe + " " + psi.Arguments);

            var outBuf = new StringBuilder();
            var errBuf = new StringBuilder();
            using (var p = new Process { StartInfo = psi })
            {
                p.OutputDataReceived += (s, e) => { if (e.Data != null) outBuf.AppendLine(e.Data); };
                p.ErrorDataReceived += (s, e) => { if (e.Data != null) errBuf.AppendLine(e.Data); };
                p.Start();
                p.BeginOutputReadLine();
                p.BeginErrorReadLine();
                p.WaitForExit();
                return new ProcResult { ExitCode = p.ExitCode, StdOut = outBuf.ToString(), StdErr = errBuf.ToString() };
            }
        }

        /// <summary>Echo a tool's diagnostics to the MSBuild log as errors/warnings/messages.</summary>
        protected void LogDiagnostics(string text)
        {
            if (string.IsNullOrEmpty(text)) return;
            foreach (var raw in text.Split('\n'))
            {
                string line = raw.TrimEnd('\r');
                if (line.Length == 0) continue;

                Match m = FileDiag.Match(line);
                if (m.Success)
                {
                    string file = m.Groups["file"].Value;
                    int ln = int.Parse(m.Groups["line"].Value);
                    int col = int.Parse(m.Groups["col"].Value);
                    string msg = m.Groups["msg"].Value;
                    if (m.Groups["sev"].Value == "error")
                        Log.LogError(null, null, null, file, ln, col, 0, 0, msg);
                    else if (m.Groups["sev"].Value == "warning")
                        Log.LogWarning(null, null, null, file, ln, col, 0, 0, msg);
                    else
                        Log.LogMessage(MessageImportance.Normal, line);
                    continue;
                }
                m = ToolDiag.Match(line);
                if (m.Success)
                {
                    if (m.Groups["sev"].Value == "error")
                        Log.LogError("{0}: {1}", m.Groups["tool"].Value, m.Groups["msg"].Value);
                    else
                        Log.LogWarning("{0}: {1}", m.Groups["tool"].Value, m.Groups["msg"].Value);
                    continue;
                }
                Log.LogMessage(MessageImportance.Normal, line);
            }
        }

        /// <summary>Quote arguments for a Windows command line (only where needed).</summary>
        protected static string JoinArgs(IEnumerable<string> args)
        {
            var sb = new StringBuilder();
            foreach (var a in args)
            {
                if (sb.Length > 0) sb.Append(' ');
                sb.Append(Quote(a));
            }
            return sb.ToString();
        }

        private static string Quote(string a)
        {
            if (a.Length > 0 && a.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return a;
            // Backslash/quote escaping per CommandLineToArgvW.
            var sb = new StringBuilder("\"");
            int slashes = 0;
            foreach (char c in a)
            {
                if (c == '\\') { slashes++; continue; }
                if (c == '"') { sb.Append('\\', slashes * 2 + 1); slashes = 0; }
                else { sb.Append('\\', slashes); slashes = 0; }
                sb.Append(c);
            }
            sb.Append('\\', slashes * 2);
            sb.Append('"');
            return sb.ToString();
        }
    }
}
