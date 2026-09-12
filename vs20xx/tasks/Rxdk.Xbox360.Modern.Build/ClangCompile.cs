// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.IO;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Rxdk.Xbox360.Modern.Build
{
    /// <summary>
    /// Compile C/C++/asm sources with the patched MS-PPC clang, one object per
    /// source. Mirrors mktitle.py's compile_sources + auto_clang_flags: C sources
    /// get the picolibc environment, C++ sources additionally get the exception/
    /// RTTI flags and the libc++/libc++abi headers (before picolibc so &lt;cstdlib&gt;
    /// etc. resolve to libc++'s wrappers). Prebuilt .o sources pass through.
    /// </summary>
    public sealed class ClangCompile : ModernTool
    {
        [Required] public string ClangPath { get; set; }

        /// <summary>Install root that holds runtime/, vendor/ (for the auto include set).</summary>
        [Required] public string RxdkRoot { get; set; }

        /// <summary>Where the .o files are written (the project IntDir).</summary>
        [Required] public string OutputDir { get; set; }

        [Required] public ITaskItem[] Sources { get; set; }

        public string MsTriple { get; set; } = "powerpc-unknown-xbox360";
        public string Optimization { get; set; } = "-O2";
        public string LanguageStandardC { get; set; } = "c23";
        public string LanguageStandardCpp { get; set; } = "c++23";
        public string[] AdditionalIncludeDirectories { get; set; }
        public string[] PreprocessorDefinitions { get; set; }
        public string AdditionalOptions { get; set; }

        /// <summary>The compiled objects (fed to ClangLink).</summary>
        [Output] public ITaskItem[] ObjectFiles { get; set; }

        public override bool Execute()
        {
            Directory.CreateDirectory(OutputDir);
            var objects = new List<ITaskItem>();
            bool ok = true;

            foreach (var item in Sources)
            {
                string src = item.GetMetadata("FullPath");
                string ext = Path.GetExtension(src).ToLowerInvariant();
                if (ext == ".o" || ext == ".obj")
                {
                    objects.Add(new TaskItem(src));
                    continue;
                }

                string obj = Path.Combine(OutputDir, ObjectName(src));
                objects.Add(new TaskItem(obj));

                if (UpToDate(src, obj))
                {
                    Log.LogMessage(MessageImportance.Low, "  up to date: " + Path.GetFileName(src));
                    continue;
                }

                Log.LogMessage(MessageImportance.High, "  " + Path.GetFileName(src));
                bool isAsm = ext == ".s" || ext == ".asm";
                bool isCpp = ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++";

                var args = new List<string> { "--target=" + MsTriple, "-c", src, "-o", obj };
                if (!isAsm)
                {
                    // Insert compile flags before the -c (matching mktitle's ordering).
                    var pre = new List<string> { Optimization, "-std=" + (isCpp ? LanguageStandardCpp : LanguageStandardC) };
                    pre.AddRange(AutoClangFlags(isCpp));
                    if (AdditionalIncludeDirectories != null)
                        foreach (var inc in AdditionalIncludeDirectories)
                            if (!string.IsNullOrWhiteSpace(inc)) pre.Add("-I" + inc.Trim());
                    if (PreprocessorDefinitions != null)
                        foreach (var d in PreprocessorDefinitions)
                            if (!string.IsNullOrWhiteSpace(d)) pre.Add("-D" + d.Trim());
                    if (!string.IsNullOrWhiteSpace(AdditionalOptions))
                        foreach (var opt in SplitOptions(AdditionalOptions))
                        {
                            // Drop MSVC-style (/...) options: this is a gcc-style clang
                            // toolset, and the stock platform props inject cl.exe defaults
                            // (e.g. the /FI IntelliSense forced-include) that clang rejects.
                            if (opt.StartsWith("/"))
                                Log.LogMessage(MessageImportance.Low, "  (ignoring MSVC-style option " + opt + ")");
                            else
                                pre.Add(opt);
                        }
                    args.InsertRange(1, pre);   // after "--target=..."
                }

                var r = Run(ClangPath, args);
                LogDiagnostics(r.Combined);
                if (r.ExitCode != 0)
                {
                    Log.LogError("clang: compile failed for {0}", Path.GetFileName(src));
                    ok = false;
                }
            }

            ObjectFiles = objects.ToArray();
            return ok && !Log.HasLoggedErrors;
        }

        /// <summary>
        /// The standard modern-runtime compile environment (see mktitle.auto_clang_flags).
        /// Placed before the user's options so those still win on any conflict.
        /// </summary>
        private IEnumerable<string> AutoClangFlags(bool isCpp)
        {
            string cfg = Path.Combine(RxdkRoot, "runtime", "config");
            string pico = Path.Combine(RxdkRoot, "vendor", "picolibc", "libc", "include");
            if (!isCpp)
                return new[] { "-D__Picolibc__", "-D_GNU_SOURCE", "-I" + cfg, "-I" + pico,
                               "-include", "picolibc.h" };
            string lx = Path.Combine(RxdkRoot, "vendor", "llvm-project", "libcxx", "include");
            string la = Path.Combine(RxdkRoot, "vendor", "llvm-project", "libcxxabi", "include");
            return new[] { "-fexceptions", "-funwind-tables", "-frtti",
                           "-D__Picolibc__", "-D_GNU_SOURCE",
                           "-I" + lx, "-I" + la, "-I" + cfg,
                           "-include", "__config_site", "-include", "rxdk_libcpp_prereq.h",
                           "-I" + pico, "-include", "picolibc.h" };
        }

        // Qualify the object with its parent directory: many titles carry several
        // sources all named main.c (one per subdir), which would otherwise collide.
        private static string ObjectName(string src)
        {
            string stem = Path.GetFileNameWithoutExtension(src);
            string parent = Path.GetFileName(Path.GetDirectoryName(Path.GetFullPath(src)));
            return (string.IsNullOrEmpty(parent) ? stem : parent + "_" + stem) + ".o";
        }

        private static bool UpToDate(string src, string obj)
        {
            return File.Exists(obj) && File.GetLastWriteTimeUtc(obj) >= File.GetLastWriteTimeUtc(src);
        }

        private static IEnumerable<string> SplitOptions(string s)
        {
            // Split on whitespace outside quotes.
            var list = new List<string>();
            var cur = new System.Text.StringBuilder();
            bool q = false;
            foreach (char c in s)
            {
                if (c == '"') q = !q;
                else if (char.IsWhiteSpace(c) && !q)
                {
                    if (cur.Length > 0) { list.Add(cur.ToString()); cur.Clear(); }
                }
                else cur.Append(c);
            }
            if (cur.Length > 0) list.Add(cur.ToString());
            return list;
        }
    }
}
