// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Rxdk.Xbox360.Modern.Build
{
    /// <summary>
    /// Link compiled objects into a bootable XEX with the modern (LLVM) toolchain,
    /// resolving the kernel imports a title calls. A faithful MSBuild port of
    /// mktitle.py's link stage: write the layout script, trial-link with ld.lld to
    /// discover the undefined kernel symbols, run XexTool genstubs to synthesise
    /// linkable import thunks + a JSON import manifest, assemble the thunks with
    /// clang, link the final ELF, then pack it with XexTool.
    /// </summary>
    public sealed class ClangLink : ModernTool
    {
        [Required] public string ClangPath { get; set; }
        [Required] public string LldPath { get; set; }
        [Required] public string XexToolPath { get; set; }

        [Required] public ITaskItem[] Objects { get; set; }

        /// <summary>Output .xex.</summary>
        [Required] public string OutputFile { get; set; }

        /// <summary>Scratch dir for the .ld / .elf / stubs (the project IntDir).</summary>
        [Required] public string IntDir { get; set; }

        /// <summary>XDK lib\xbox directory (short-import members -&gt; console ordinals).</summary>
        [Required] public string XdkLibDir { get; set; }

        /// <summary>Where bare library names resolve (coff2elf archives).</summary>
        public string CoffDir { get; set; }
        /// <summary>Where the modern runtime archives (libc.a/libcpp.a) live.</summary>
        public string LibcDir { get; set; }

        public string MsTriple { get; set; } = "powerpc-unknown-xbox360";
        public string BaseAddress { get; set; } = "0x82000000";

        /// <summary>Extra libraries: bare name -&gt; CoffDir\name.a, or a full path/.a.</summary>
        public string[] Libraries { get; set; }
        /// <summary>Do not auto-link the modern runtime (libc.a, libcpp.a, xapilib.a).</summary>
        public bool NoDefaultLibs { get; set; }

        public string AdditionalOptions { get; set; }
        public bool KeepElf { get; set; }

        private static readonly Regex UndefinedRe = new Regex(@"undefined symbol: (\S+)", RegexOptions.Compiled);
        private static readonly Regex UnresolvedRe = new Regex(@"unresolved \(not kernel imports\): (.+)", RegexOptions.Compiled);

        public override bool Execute()
        {
            Directory.CreateDirectory(IntDir);
            string outBase = Path.Combine(IntDir, Path.GetFileNameWithoutExtension(OutputFile));
            string layout = outBase + ".ld";
            string elf = outBase + ".elf";

            uint baseAddr = ParseBase(BaseAddress);
            int page = baseAddr < 0x90000000u ? 0x10000 : 0x1000;
            File.WriteAllText(layout, Layout(baseAddr, page));

            var objs = new List<string>();
            foreach (var o in Objects) objs.Add(o.GetMetadata("FullPath"));

            var libs = ResolveLibraries();
            foreach (var lib in libs)
                if (!File.Exists(lib)) { Log.LogError("library not found: {0}", lib); return false; }

            var ldflags = ExtraLinkerArgs();

            // Trial link (no stubs): its undefined-symbol list IS the kernel-import
            // discovery. --error-limit=0 so the full list is reported; --no-demangle
            // so C++ names stay intact for genstubs.
            var trial = LinkElf(objs, libs, null, layout, elf, ldflags);
            var undefined = trial.ExitCode != 0 ? UndefinedFrom(trial.Combined) : new List<string>();

            string manifest = null, stubsObj = null;
            if (undefined.Count > 0)
            {
                string stubsS = outBase + "_stubs.s";
                manifest = outBase + "_stubs.json";
                var gs = Run(XexToolPath, new[] { "genstubs", "--xdk", XdkLibDir,
                    "--names", string.Join(",", undefined), "-o", stubsS, "--manifest", manifest });
                Log.LogMessage(MessageImportance.Normal, gs.StdOut);
                if (gs.ExitCode != 0)
                {
                    LogDiagnostics(gs.Combined);
                    Log.LogError("genstubs failed");
                    return false;
                }
                // A symbol that is not a kernel export is a genuine link error.
                var m = UnresolvedRe.Match(gs.StdOut);
                if (m.Success)
                {
                    Log.LogError("unresolved symbols (not kernel imports): {0}", m.Groups[1].Value.Trim());
                    return false;
                }
                // ld.lld links objects, not assembly: assemble the thunks first.
                stubsObj = outBase + "_stubs.o";
                var asm = Run(ClangPath, new[] { "--target=" + MsTriple, "-c", stubsS, "-o", stubsObj });
                if (asm.ExitCode != 0)
                {
                    LogDiagnostics(asm.Combined);
                    Log.LogError("assembling import stubs failed");
                    return false;
                }
            }

            // Final link.
            var final = LinkElf(objs, libs, stubsObj, layout, elf, ldflags);
            if (final.ExitCode != 0)
            {
                LogDiagnostics(final.Combined);
                Log.LogError("link failed");
                return false;
            }

            // Pack the ELF into a devkit XEX2.
            var packArgs = new List<string> { "pack", elf, "-o", OutputFile };
            if (manifest != null) { packArgs.Add("--import-manifest"); packArgs.Add(manifest); }
            var pack = Run(XexToolPath, packArgs);
            Log.LogMessage(MessageImportance.Normal, pack.StdOut);
            if (pack.ExitCode != 0)
            {
                LogDiagnostics(pack.Combined);
                Log.LogError("pack failed");
                return false;
            }

            if (!KeepElf && File.Exists(elf)) File.Delete(elf);
            Log.LogMessage(MessageImportance.High, "  -> " + OutputFile);
            return !Log.HasLoggedErrors;
        }

        private ProcResult LinkElf(List<string> objs, List<string> libs, string stubs,
                                   string layout, string elf, List<string> ldflags)
        {
            var args = new List<string> { "-T", layout, "-e", "_start", "--error-limit=0",
                                          "--no-demangle", "--gc-sections" };
            args.AddRange(ldflags);
            args.AddRange(objs);
            if (stubs != null) args.Add(stubs);
            if (libs.Count > 0)
            {
                args.Add("--start-group");
                args.AddRange(libs);
                args.Add("--end-group");
            }
            args.Add("-o");
            args.Add(elf);
            return Run(LldPath, args);
        }

        private List<string> ResolveLibraries()
        {
            var user = new List<string>();
            if (Libraries != null)
                foreach (var spec in Libraries)
                    foreach (var n in spec.Split(','))
                    {
                        var name = n.Trim();
                        if (name.Length == 0) continue;
                        if (name.IndexOf(Path.DirectorySeparatorChar) >= 0 || name.EndsWith(".a") || Path.IsPathRooted(name))
                            user.Add(name);
                        else
                            user.Add(Path.Combine(CoffDir ?? "", name + ".a"));
                    }

            // User libs first, runtime after - matching the official link order
            // (title libs, then CRT). libcpp.a is always included: libc.a itself
            // references the C++ runtime, and the two are linked as a group.
            if (!NoDefaultLibs)
            {
                if (!string.IsNullOrEmpty(LibcDir))
                {
                    user.Add(Path.Combine(LibcDir, "libcpp.a"));
                    user.Add(Path.Combine(LibcDir, "libc.a"));
                }
                if (!string.IsNullOrEmpty(CoffDir))
                    user.Add(Path.Combine(CoffDir, "xapilib.a"));
            }
            return user;
        }

        // Unwrap a caller-supplied -Wl,a,b into raw linker arguments (the old zig
        // driver form), else pass the flag through verbatim.
        private List<string> ExtraLinkerArgs()
        {
            var outv = new List<string>();
            if (string.IsNullOrWhiteSpace(AdditionalOptions)) return outv;
            foreach (var f in AdditionalOptions.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries))
            {
                if (f.StartsWith("-Wl,"))
                    outv.AddRange(f.Substring(4).Split(','));
                else
                    outv.Add(f);
            }
            return outv;
        }

        private static List<string> UndefinedFrom(string text)
        {
            var seen = new List<string>();
            foreach (Match m in UndefinedRe.Matches(text))
            {
                string name = m.Groups[1].Value;
                if (!seen.Contains(name)) seen.Add(name);
            }
            return seen;
        }

        private static uint ParseBase(string s)
        {
            s = s.Trim();
            if (s.StartsWith("0x") || s.StartsWith("0X"))
                return Convert.ToUInt32(s.Substring(2), 16);
            return Convert.ToUInt32(s, 10);
        }

        /// <summary>The layout the packer expects (see mktitle.write_layout).</summary>
        private static string Layout(uint baseAddr, int page)
        {
            return
$@"ENTRY(_start)
SECTIONS {{
  . = 0x{baseAddr:X8};
  . += 0x1000;                       /* room for the synthesised PE headers */
  .text   : {{ *(.text*) }}
  . = ALIGN(0x{page:X});             /* read-only data on its own page */
  .rodata : {{
    *(.rodata*)
    *(.gcc_except_table .gcc_except_table.*)
  }}
  .pdata : {{                        /* RUNTIME_FUNCTION table (exact fn bounds) */
    *(.pdata)
    *(.pdata.*)
  }}
  .xdata : {{                        /* unwind info the .pdata entries point at */
    *(.xdata)
    *(.xdata.*)
  }}
  . = ALIGN(0x{page:X});             /* DWARF EH tables on their own read-only page */
  .eh_frame : {{
    PROVIDE_HIDDEN(__eh_frame_start = .);
    KEEP(*(.eh_frame))
    KEEP(*(.eh_frame.*))
    PROVIDE_HIDDEN(__eh_frame_end = .);
    PROVIDE_HIDDEN(__eh_frame_hdr_start = .);
    PROVIDE_HIDDEN(__eh_frame_hdr_end = .);
  }}
  .init_array : {{                   /* C++ static constructors, run pre-main */
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN(__init_array_end = .);
  }}
  .fini_array : {{                   /* destructors (kept writable, off the CODE page) */
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    PROVIDE_HIDDEN(__fini_array_end = .);
  }}
  .CRT : {{                          /* MS CRT initializer table (.CRT$XCA..XCZ) */
    PROVIDE_HIDDEN(__xc_a = .);
    KEEP(*(SORT_BY_NAME(.CRT$XC*)))
    PROVIDE_HIDDEN(__xc_z = .);
  }}
  .kvars  : {{ KEEP(*(.kvars)) }}    /* import var records: keep past --gc-sections */
  . = ALIGN(0x{page:X});             /* import thunks on their own CODE page */
  .kthunks : ALIGN(16) {{ KEEP(*(.kthunks)) }}
  . = ALIGN(0x{page:X});             /* writable region on its own page(s) */
  .data : {{ *(.data*) }}
  .bss  : {{ *(.bss*) *(COMMON) }}
  /DISCARD/ : {{ *(.comment) *(.note*) }}
}}
";
        }
    }
}
