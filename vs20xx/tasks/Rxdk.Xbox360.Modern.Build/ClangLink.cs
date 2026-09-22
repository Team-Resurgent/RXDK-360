// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
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

        // Xbox 360 Image Conversion page (same metadata as legacy imagexex).
        public string TitleID { get; set; }
        public string LanKey { get; set; }
        public string HeapSize { get; set; }
        public string WorkspaceSize { get; set; }
        public bool ExportByName { get; set; }
        public bool OpticalDiscDriveMapping { get; set; }
        public bool Pal50Incompatible { get; set; }
        public bool MultiDiscTitle { get; set; }
        public bool PreferBigButtonInput { get; set; }
        public bool CrossPlatformSystemLink { get; set; }
        public bool AllowAvatarGetMetadata { get; set; }
        public bool AllowControllerSwapping { get; set; }
        public bool RequireFullExperience { get; set; }
        public bool GameVoiceRequiredUI { get; set; }
        public bool KinectElevationControl { get; set; }
        public string KinectSupportLevel { get; set; }
        public string ConfigurationFile { get; set; }
        public string[] AdditionalSections { get; set; }

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

            // Pack the ELF into a XEX2, then debug-sign it. A zero RSA signature
            // is classified Retail; this kit then returns LDRX C000007B. `-m d`
            // fills hashes and the debug signature (observed required for load).
            string packed = OutputFile + ".unsigned.xex";
            var packArgs = new List<string> { "pack", elf, "-o", packed };
            if (manifest != null) { packArgs.Add("--import-manifest"); packArgs.Add(manifest); }
            var pack = Run(XexToolPath, packArgs);
            Log.LogMessage(MessageImportance.Normal, pack.StdOut);
            if (pack.ExitCode != 0)
            {
                LogDiagnostics(pack.Combined);
                Log.LogError("pack failed");
                return false;
            }

            string xml = Path.Combine(IntDir, Path.GetFileNameWithoutExtension(OutputFile) + ".xex.xml");
            if (!WriteImageXexXml(xml))
                return false;
            if (File.Exists(xml))
            {
                var apply = Run(XexToolPath, new[] { "applyxml", packed, "--xml", xml, "-o", packed });
                Log.LogMessage(MessageImportance.Normal, apply.StdOut);
                if (apply.ExitCode != 0)
                {
                    LogDiagnostics(apply.Combined);
                    Log.LogError("applyxml failed (Image Conversion settings)");
                    return false;
                }
            }

            var sign = Run(XexToolPath, new[] { "-m", "d", "-o", OutputFile, packed });
            Log.LogMessage(MessageImportance.Normal, sign.StdOut);
            try { File.Delete(packed); } catch { }
            if (sign.ExitCode != 0)
            {
                LogDiagnostics(sign.Combined);
                Log.LogError("XexTool -m d failed");
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
                                          "--no-dependent-libraries",
                                          // Our modern CRT (libc.a, scanned first) shadows the XDK's
                                          // MSVC CRT, but XDK import archives bundle CRT-level symbols
                                          // (ceilf, wsprintf, ...) into members pulled for other code,
                                          // so the same symbol can be defined in both. Take the first
                                          // definition (our runtime) instead of erroring; symbols only
                                          // the XDK provides are unaffected.
                                          "--allow-multiple-definition" };
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

            // The modern C/C++ runtime (libc.a, libcpp.a) is implicit, like libcmt.
            // It is placed FIRST so it is scanned ahead of the XDK import archives:
            // both --start-group members can define the same CRT-level symbol (e.g.
            // xapilib.a ships MSVC-built wsprintf/wvsprintf, which read a clang
            // va_list as garbage), and within a group lld takes the definition from
            // the first archive in command-line order. Runtime-first makes our
            // clang-built CRT win those collisions; symbols only the XDK provides
            // still resolve because the group is rescanned until stable. xapilib.a
            // is appended (after title libs) so only its non-CRT members are pulled.
            var pre = new List<string>();
            if (!NoDefaultLibs)
            {
                if (!string.IsNullOrEmpty(LibcDir))
                {
                    pre.Add(Path.Combine(LibcDir, "libcpp.a"));
                    pre.Add(Path.Combine(LibcDir, "libc.a"));
                }
                if (!string.IsNullOrEmpty(CoffDir))
                    user.Add(Path.Combine(CoffDir, "xapilib.a"));
            }

            // De-dupe by normalised full path, keeping first occurrence (so the
            // runtime copies in `pre` take precedence over any listed by the title).
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var result = new List<string>();
            foreach (var l in pre)
            {
                string key;
                try { key = Path.GetFullPath(l); } catch { key = l; }
                if (seen.Add(key)) result.Add(l);
            }
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

        /// <summary>
        /// Write the imagexex-style XML that applyxml consumes. Returns false on
        /// a hard error (missing Additional Sections file, etc.). Returns true
        /// and omits the file when there is nothing to apply.
        /// </summary>
        private bool WriteImageXexXml(string path)
        {
            var body = new StringBuilder();
            if (!string.IsNullOrEmpty(ConfigurationFile) && File.Exists(ConfigurationFile))
            {
                // Pull child elements out of the user's imagexex config so one
                // applyxml pass sees both the file and the property-page flags.
                string raw = File.ReadAllText(ConfigurationFile);
                int open = raw.IndexOf("<xex", StringComparison.OrdinalIgnoreCase);
                int inner = open >= 0 ? raw.IndexOf('>', open) : -1;
                int close = raw.LastIndexOf("</xex>", StringComparison.OrdinalIgnoreCase);
                if (inner > 0 && close > inner)
                    body.Append(raw.Substring(inner + 1, close - inner - 1).Trim());
                else
                    Log.LogWarning("Image Conversion Configuration File is not an imagexex <xex> XML; ignoring {0}", ConfigurationFile);
            }
            else if (!string.IsNullOrEmpty(ConfigurationFile))
            {
                Log.LogError("Image Conversion Configuration File not found: {0}", ConfigurationFile);
                return false;
            }

            void Tag(string line)
            {
                if (body.Length > 0 && body[body.Length - 1] != '\n') body.AppendLine();
                body.AppendLine(line);
            }

            if (!string.IsNullOrWhiteSpace(TitleID))
                Tag("  <titleid id=\"" + TitleID.Trim() + "\"/>");
            if (!string.IsNullOrWhiteSpace(LanKey))
                Tag("  <lankey id=\"" + LanKey.Trim() + "\"/>");
            if (!string.IsNullOrWhiteSpace(HeapSize))
                Tag("  <xapiheap size=\"" + HeapSize.Trim() + "\"/>");
            if (!string.IsNullOrWhiteSpace(WorkspaceSize))
                Tag("  <workspace size=\"" + WorkspaceSize.Trim() + "\"/>");
            if (ExportByName) Tag("  <exportnames/>");
            // Privilege 2 = No ODD Mapping. applyxml drops DVD/CD (and XGD2)
            // so the XEX matches a pack imagexex accepts; /privilege:2 alone is IM1069.
            if (OpticalDiscDriveMapping) Tag("  <privilege id=\"2\"/>");
            if (Pal50Incompatible) Tag("  <privilege id=\"10\"/>");
            if (MultiDiscTitle)
            {
                Tag("  <privilege id=\"15\"/>");
                Tag("  <privilege id=\"16\"/>");
            }
            if (PreferBigButtonInput) Tag("  <privilege id=\"25\"/>");
            if (CrossPlatformSystemLink) Tag("  <privilege id=\"14\"/>");
            if (AllowAvatarGetMetadata) Tag("  <privilege id=\"29\"/>");
            if (AllowControllerSwapping) Tag("  <privilege id=\"30\"/>");
            if (RequireFullExperience) Tag("  <privilege id=\"34\"/>");
            if (GameVoiceRequiredUI) Tag("  <privilege id=\"35\"/>");
            if (KinectElevationControl) Tag("  <privilege id=\"37\"/>");
            if (string.Equals(KinectSupportLevel, "RequiresTracking", StringComparison.OrdinalIgnoreCase))
                Tag("  <privilege id=\"38\"/>");
            else if (string.Equals(KinectSupportLevel, "SupportsTracking", StringComparison.OrdinalIgnoreCase))
                Tag("  <privilege id=\"39\"/>");

            if (AdditionalSections != null)
            {
                foreach (var raw in AdditionalSections)
                {
                    if (string.IsNullOrWhiteSpace(raw))
                        continue;
                    string spec = raw.Trim();
                    string secName;
                    string file;
                    int eq = spec.IndexOf('=');
                    if (eq > 0)
                    {
                        secName = spec.Substring(0, eq).Trim();
                        file = spec.Substring(eq + 1).Trim();
                    }
                    else
                    {
                        file = spec;
                        secName = Path.GetFileNameWithoutExtension(file);
                    }
                    if (string.IsNullOrEmpty(secName) || string.IsNullOrEmpty(file))
                    {
                        Log.LogError("Additional Sections entry must be NAME=file (imagexex /section:), not '{0}'", spec);
                        return false;
                    }
                    if (!File.Exists(file))
                    {
                        Log.LogError("Additional Section file not found: {0}", file);
                        return false;
                    }
                    Tag("  <section name=\"" + XmlAttr(secName) + "\" file=\"" + XmlAttr(Path.GetFullPath(file)) + "\"/>");
                }
            }

            if (body.Length == 0) return true;

            Directory.CreateDirectory(Path.GetDirectoryName(path) ?? ".");
            File.WriteAllText(path, "<xex>\n" + body.ToString().TrimEnd() + "\n</xex>\n");
            Log.LogMessage(MessageImportance.Low, "  Image Conversion XML " + path);
            return true;
        }

        static string XmlAttr(string s)
        {
            if (string.IsNullOrEmpty(s)) return "";
            return s.Replace("&", "&amp;").Replace("\"", "&quot;").Replace("<", "&lt;").Replace(">", "&gt;");
        }

        /// <summary>The layout the packer expects (see mktitle.write_layout).</summary>
        private static string Layout(uint baseAddr, int page)
        {
            return
$@"ENTRY(_start)
SECTIONS {{
  . = 0x{baseAddr:X8};
  . += 0x1000;                       /* PE headers; first 64KB page is headers + RO */
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
  .eh_frame : {{
    PROVIDE_HIDDEN(__eh_frame_start = .);
    KEEP(*(.eh_frame))
    KEEP(*(.eh_frame.*))
    PROVIDE_HIDDEN(__eh_frame_end = .);
    PROVIDE_HIDDEN(__eh_frame_hdr_start = .);
    PROVIDE_HIDDEN(__eh_frame_hdr_end = .);
  }}
  . = ALIGN(0x{page:X});             /* CODE after RO: ImageXex IM1031 if RX shares the header page */
  .text   : {{ *(.text*) }}
  . = ALIGN(0x{page:X});             /* import thunks on their own CODE page */
  .kthunks : ALIGN(16) {{ KEEP(*(.kthunks)) }}
  . = ALIGN(0x{page:X});             /* writable region: IAT then data. HV patches .kvars in place (C0000225 if RO). */
  .kvars  : {{ KEEP(*(.kvars)) }}
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
  .data : {{
    /* PPC TOC base. lld synthesizes .TOC. for a PPC64 target but not this PPC32
       one, so some objects (OpenMP outlines, XLSP) reference it unresolved.
       Define it at the conventional GOT+0x8000 anchor. */
    PROVIDE_HIDDEN(.TOC. = . + 0x8000);
    *(.data*)
  }}
  .bss  : {{ *(.bss*) *(COMMON) }}
  /DISCARD/ : {{ *(.comment) *(.note*) }}
}}
";
        }
    }
}
