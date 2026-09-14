// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.IO;
using System.Reflection;

namespace Rxdk360.Package.Services
{
    internal static class ToolLocator
    {
        public const string DapExeName = "Rxdk.Xbox360.DebugAdapter.exe";
        private const string ToolsDirEnvVar = "RXDK360_TOOLS_DIR";

        public static string ResolveDap()
        {
            var overrideDir = Environment.GetEnvironmentVariable(ToolsDirEnvVar);
            if (!string.IsNullOrEmpty(overrideDir))
            {
                var p = Path.Combine(overrideDir, DapExeName);
                if (File.Exists(p)) return p;
            }

            var vsixDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            if (!string.IsNullOrEmpty(vsixDir))
            {
                foreach (var rel in new[]
                {
                    Path.Combine("tools", DapExeName),
                    DapExeName,
                })
                {
                    var p = Path.Combine(vsixDir, rel);
                    if (File.Exists(p)) return p;
                }
            }

            var dir = vsixDir;
            for (int i = 0; i < 8 && dir != null; i++)
            {
                foreach (var cfg in new[] { "Debug", "Release" })
                {
                    var p = Path.Combine(dir, "debugger", "Rxdk.Xbox360.DebugAdapter",
                        "bin", cfg, "net10.0-windows", "win-x86", DapExeName);
                    if (File.Exists(p)) return p;
                }
                dir = Path.GetDirectoryName(dir);
            }
            return null;
        }
    }
}
