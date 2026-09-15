// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Diagnostics;
using System.IO;
using System.Text;

namespace Rxdk360.Package.Services
{
    /// <summary>
    /// Same Emulate DVD sequence as VS 2010 X360EmulationManagerV100: generate a
    /// .xgd from the title XEX, then xbEmulate /Emulate start. USB-sidecar miss
    /// uses the stock dialog text from that DLL.
    /// </summary>
    internal static class DvdEmulation
    {
        public static string SessionFailedMessage(string console)
        {
            string who = string.IsNullOrWhiteSpace(console) ? "the default console" : console.Trim();
            return "Failed to create emulation session for '" + who + "'.\r\n" +
                   "Ensure that this computer is connected via USB to the port labeled 'DVD EMU' on the console.";
        }

        public static string WriteGeneratedLayout(string stagingDir, string xgdPath, string imagePath)
        {
            if (Directory.Exists(stagingDir))
                Directory.Delete(stagingDir, true);
            Directory.CreateDirectory(stagingDir);
            if (string.IsNullOrEmpty(imagePath) || !File.Exists(imagePath))
                throw new InvalidOperationException("No XEX to put on the emulated disc.");
            string name = Path.GetFileName(imagePath);
            File.Copy(imagePath, Path.Combine(stagingDir, "default.xex"), true);
            if (!name.Equals("default.xex", StringComparison.OrdinalIgnoreCase))
                File.Copy(imagePath, Path.Combine(stagingDir, name), true);

            string source = stagingDir.TrimEnd('\\') + "\\";
            var sb = new StringBuilder();
            sb.AppendLine("<LAYOUT MAJORVERSION=\"2\" MINORVERSION=\"2\">");
            sb.AppendLine("    <AVATARASSETPACK INCLUDE=\"NO\"/>");
            sb.AppendLine("    <DISC NAME=\"\" LayoutType=\"XGD2\">");
            sb.Append("        <ADD NAME=\"\" SOURCE=\"");
            sb.Append(EscapeXml(source));
            sb.AppendLine("\" DEST=\"\\\" FILESPEC=\"*.*\" RECURSE=\"YES\" LAYER=\"ANY\" ALIGN=\"1\"/>");
            sb.AppendLine("    </DISC>");
            sb.AppendLine("</LAYOUT>");
            Directory.CreateDirectory(Path.GetDirectoryName(xgdPath) ?? stagingDir);
            File.WriteAllText(xgdPath, sb.ToString(), new UTF8Encoding(false));
            return xgdPath;
        }

        public static bool TryStart(string xbEmulate, string xgd, string timingMode, string console, out string error, out string log)
        {
            error = "";
            var psi = new ProcessStartInfo
            {
                FileName = xbEmulate,
                Arguments = "/nologo /Media \"" + xgd + "\" /TimingMode " + timingMode + " /Emulate start",
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
            };
            if (!string.IsNullOrEmpty(console) && !LooksLikeIp(console))
                psi.Arguments = "/nologo /Console \"" + console + "\" /Media \"" + xgd +
                    "\" /TimingMode " + timingMode + " /Emulate start";

            var output = new StringBuilder();
            using (var p = Process.Start(psi))
            {
                if (p == null)
                {
                    error = SessionFailedMessage(console);
                    log = "";
                    return false;
                }
                output.AppendLine(p.StandardOutput.ReadToEnd());
                output.AppendLine(p.StandardError.ReadToEnd());
                p.WaitForExit();
                log = output.ToString();
                if (p.ExitCode == 0 && log.IndexOf("ERROR:", StringComparison.OrdinalIgnoreCase) < 0)
                    return true;
            }
            error = SessionFailedMessage(console);
            return false;
        }

        public static string TimingMode(string type)
        {
            if (string.Equals(type, "TypicalSeekTimes", StringComparison.OrdinalIgnoreCase))
                return "typical";
            if (string.Equals(type, "AccurateSeekTimes", StringComparison.OrdinalIgnoreCase))
                return "accurate";
            return "none";
        }

        public static string FindXbEmulate()
        {
            foreach (var root in new[]
            {
                Environment.GetEnvironmentVariable("RXDK360"),
                Environment.GetEnvironmentVariable("XEDK"),
                @"C:\Program Files\RXDK-360",
                @"C:\Program Files (x86)\Microsoft Xbox 360 SDK",
            })
            {
                if (string.IsNullOrEmpty(root)) continue;
                string p = Path.Combine(root, "bin", "win32", "xbEmulate.exe");
                if (File.Exists(p)) return p;
            }
            return null;
        }

        private static bool LooksLikeIp(string s)
        {
            int dots = 0;
            foreach (char c in s)
                if (c == '.') dots++;
                else if (c != ':' && !char.IsDigit(c)) return false;
            return dots == 3;
        }

        private static string EscapeXml(string s)
        {
            if (string.IsNullOrEmpty(s)) return "";
            return s.Replace("&", "&amp;").Replace("\"", "&quot;").Replace("<", "&lt;").Replace(">", "&gt;");
        }
    }
}
