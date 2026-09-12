// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// Progress reporter: writes each line to the console AND (when a progress file is
// set via --progress) appends it there, flushed, so a host installer can tail it
// and show progress in its own UI.

using System.IO;

namespace Rxdk.Xdk.Unpacker
{
    internal static class Report
    {
        public static string File;   // optional progress file (from --progress)

        public static void Line(string s)
        {
            System.Console.WriteLine(s);
            if (File != null)
            {
                try
                {
                    using (var w = new StreamWriter(new FileStream(File, FileMode.Append, FileAccess.Write, FileShare.ReadWrite)))
                        w.WriteLine(s);
                }
                catch { }
            }
        }

        public static void Line(string fmt, params object[] args)
        {
            Line(string.Format(fmt, args));
        }
    }
}
