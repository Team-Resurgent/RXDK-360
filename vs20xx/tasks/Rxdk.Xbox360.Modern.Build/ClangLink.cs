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
        /// <summary>The project's standard Linker -&gt; Input -&gt; Additional Dependencies
        /// (e.g. "xboxkrnl.lib;xapilib.lib;d3d9.lib;..."), mirroring an official title.
        /// Each ".lib" is mapped to the modern toolchain's equivalent - see
        /// ResolveLibraries.</summary>
        public string[] AdditionalDependencies { get; set; }
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

            if (KeepElf)
            {
                // Keep the DWARF-carrying ELF beside the XEX as the symbol file for
                // source-level debugging (maps onto the XEX load base).
                try
                {
                    string sym = Path.ChangeExtension(OutputFile, ".elf");
                    if (!string.Equals(Path.GetFullPath(sym), Path.GetFullPath(elf), StringComparison.OrdinalIgnoreCase))
                        File.Copy(elf, sym, true);
                }
                catch { /* symbol-file copy is best-effort */ }
            }
            else if (File.Exists(elf)) File.Delete(elf);
            Log.LogMessage(MessageImportance.High, "  -> " + OutputFile);
            return !Log.HasLoggedErrors;
        }

        private ProcResult LinkElf(List<string> objs, List<string> libs, string stubs,
                                   string layout, string elf, List<string> ldflags)
        {
            var args = new List<string> { "-T", layout, "-e", "_start", "--error-limit=0",
                                          "--no-demangle", "--gc-sections",
                                          // Ignore #pragma comment(lib, "x.lib") directives from XDK
                                          // headers - the modern link names its ELF libraries explicitly.
                                          "--no-dependent-libraries" };
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

        // XEX-import modules: on the 360 a title imports these from separate modules
        // (the kernel, xam.xex, and - dev only - xbdm.xex) via its XEX import table,
        // NOT by linking their code. Their symbols are left undefined so the trial
        // link discovers them and XexTool genstubs synthesises the imports (recorded
        // in the import manifest that 'pack --import-manifest' writes into the XEX).
        // Their .a's in the lib dir are tiny import stubs; linking one directly would
        // both duplicate the genstubs and leave the XEX with no declared import, so a
        // listed xboxkrnl.lib/xam.lib/xbdm.lib is skipped here on purpose.
        private static readonly HashSet<string> XexImportModules =
            new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "xboxkrnl", "xam", "xbdm" };

        /// <summary>
        /// Resolve the archives to link. Two sources, treated the same: Libraries
        /// (the RxdkModernLibs bare-name list) and AdditionalDependencies (the
        /// project's standard Linker Input field, mirroring an official title). Each
        /// entry maps like this:
        ///   * a full path or a *.a           -> taken as-is
        ///   * the CRT (libc/libcmt/libcpp..) -> the modern runtime archive
        ///   * an XEX-import module (xboxkrnl/xam/xbdm) -> skipped (genstubs handles it)
        ///   * anything else                  -> CoffDir\name.a (our translated code;
        ///                                       --gc-sections drops what the title
        ///                                       does not use, so over-listing is safe)
        /// The C/C++ runtime is linked implicitly, like libcmt in an official project.
        /// </summary>
        private List<string> ResolveLibraries()
        {
            var user = new List<string>();

            void AddSpec(string spec)
            {
                foreach (var n in spec.Split(',', ';'))
                {
                    var name = n.Trim();
                    if (name.Length == 0) continue;
                    // A full path or an explicit .a is taken verbatim.
                    if (name.IndexOf(Path.DirectorySeparatorChar) >= 0 || Path.IsPathRooted(name) ||
                        name.EndsWith(".a", StringComparison.OrdinalIgnoreCase))
                    { user.Add(name); continue; }
                    // Strip a trailing .lib (the Additional Dependencies form).
                    if (name.EndsWith(".lib", StringComparison.OrdinalIgnoreCase))
                        name = name.Substring(0, name.Length - 4);
                    if (name.Length == 0) continue;
                    var lower = name.ToLowerInvariant();
                    // CRT aliases (retail + debug spellings) -> the modern runtime.
                    if (lower == "libc" || lower == "libcmt" || lower == "libcmtd" || lower == "msvcrt")
                    { if (!string.IsNullOrEmpty(LibcDir)) user.Add(Path.Combine(LibcDir, "libc.a")); continue; }
                    if (lower == "libcpp" || lower == "libc++" || lower == "libcpmt" || lower == "libcpmtd")
                    { if (!string.IsNullOrEmpty(LibcDir)) user.Add(Path.Combine(LibcDir, "libcpp.a")); continue; }
                    if (lower == "xapilib" || lower == "xapilibd")
                    { if (!string.IsNullOrEmpty(CoffDir)) user.Add(Path.Combine(CoffDir, "xapilib.a")); continue; }
                    // XEX-import modules are resolved by genstubs, not linked.
                    if (XexImportModules.Contains(lower)) continue;
                    // Everything else is a translated static library.
                    user.Add(Path.Combine(CoffDir ?? "", name + ".a"));
                }
            }

            if (Libraries != null) foreach (var spec in Libraries) AddSpec(spec);
            if (AdditionalDependencies != null) foreach (var spec in AdditionalDependencies) AddSpec(spec);

            // The modern CRT is implicit (like libcmt): title libs first, runtime after.
            // libcpp.a is always included - libc.a references the C++ runtime and the
            // two are linked as a group. Kept even when the project lists its libs, and
            // de-duped below so an explicit xapilib.lib does not double-link.
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

            // De-dupe by normalised full path, keeping first occurrence. Link order
            // within the archive set does not matter: LinkElf wraps them in
            // --start-group/--end-group.
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var result = new List<string>();
            foreach (var l in user)
            {
                string key;
                try { key = Path.GetFullPath(l); } catch { key = l; }
                if (seen.Add(key)) result.Add(l);
            }
            return result;
        }

        // Unwrap a caller-supplied -Wl,a,b into raw linker arguments (the old zig
        // driver form), else pass the flag through verbatim.
        private List<string> ExtraLinkerArgs()
        {
            var outv = new List<string>();
            if (string.IsNullOrWhiteSpace(AdditionalOptions)) return outv;
            foreach (var f in AdditionalOptions.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries))
            {
                // Drop MSVC-style (/...) linker options: this toolset drives ld.lld,
                // and the stock platform props inject link.exe defaults (e.g. /XEX:NO)
                // that ld.lld does not understand.
                if (f.StartsWith("/"))
                    Log.LogMessage(MessageImportance.Low, "  (ignoring MSVC-style linker option " + f + ")");
                else if (f.StartsWith("-Wl,"))
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
