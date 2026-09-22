// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// Build the modern toolchain's global kernel import library, kernel_import.a,
// at install time from the user's own XDK import libraries. This is the C#
// twin of tools/build_import_lib.py (which the developer uses); it exists here
// because the console ordinals come from the user's XDK and clang/llvm-ar ship
// in the modern tree, so the one place with both is the install machine -- and
// the installer must not require Python.
//
// A stock XDK title links the kernel the way any program links an OS: it calls
// DbgPrint / KeSetEvent / ... and the linker resolves them from an import
// library. The public XDK ships those as COFF short-import members (name ->
// module + ordinal). This translates that WHOLE surface (xboxkrnl.exe, xam.xex,
// xbdm.xex, ...) into ONE ELF archive that every title links, each import its
// own gc-droppable COMDAT group so --gc-sections keeps exactly the thunks a
// title reaches. The record words carry the ordinal but leave module_index 0;
// the link task (ClangLink.BindKernelImports) fills the real per-title index in.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;

namespace Rxdk.Xdk.Unpacker
{
    internal static class KernelImportLib
    {
        // Module (DLL) name to record, by import-lib basename, when a short
        // import's own DLL field is empty (matches gen_import_stubs.MODULE_NAMES).
        private static readonly Dictionary<string, string> ModuleNames =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            { { "xboxkrnl", "xboxkrnl.exe" }, { "xbdm", "xbdm.xex" } };

        // Kernel exports the console has but the public XDK import libs do not
        // expose; real xboxkrnl.exe ordinals (gen_import_stubs.SUPPLEMENTAL_ORDINALS).
        private static readonly (string name, string module, int ord)[] Supplemental =
        {
            ("KeTlsAlloc", "xboxkrnl.exe", 0x152), ("KeTlsFree", "xboxkrnl.exe", 0x153),
            ("KeTlsGetValue", "xboxkrnl.exe", 0x154), ("KeTlsSetValue", "xboxkrnl.exe", 0x155),
        };

        private const ushort ImageFileMachinePowerPcBe = 0x01F2;

        private struct Imp { public string Module; public int Ordinal; public bool IsVar; }

        /// <summary>
        /// Generate <paramref name="outA"/> (kernel_import.a) from the *.lib in
        /// <paramref name="libDir"/>, assembling with clang and archiving with
        /// llvm-ar. Returns the number of imports written.
        /// </summary>
        public static int Generate(string libDir, string clangExe, string arExe, string triple, string outA)
        {
            var index = BuildOrdinalIndex(libDir);
            string asm = EmitAsm(index);
            string work = Path.GetDirectoryName(outA);
            Directory.CreateDirectory(work);
            string sPath = Path.Combine(work, "kernel_import.s");
            string oPath = Path.Combine(work, "kernel_import.o");
            // LF newlines + UTF-8 no-BOM, byte-for-byte like build_import_lib.py.
            File.WriteAllText(sPath, asm, new UTF8Encoding(false));

            RunTool(clangExe, "--target=" + triple + " -c \"" + sPath + "\" -o \"" + oPath + "\"");
            if (File.Exists(outA)) File.Delete(outA);
            RunTool(arExe, "rcs \"" + outA + "\" \"" + oPath + "\"");
            int n = 0;
            foreach (var kv in index) if (kv.Value.Ordinal != 0) n++;
            return n;
        }

        /// <summary>name -> (module, ordinal, is_var) across the XDK import libs.</summary>
        private static SortedDictionary<string, Imp> BuildOrdinalIndex(string libDir)
        {
            var index = new SortedDictionary<string, Imp>(StringComparer.Ordinal);
            foreach (var s in Supplemental)
                index[s.name] = new Imp { Module = s.module, Ordinal = s.ord, IsVar = false };

            foreach (var path in Directory.GetFiles(libDir, "*.lib"))
            {
                string baseName = Path.GetFileNameWithoutExtension(path).ToLowerInvariant();
                byte[] blob = File.ReadAllBytes(path);
                foreach (var m in ReadArchiveMembers(blob))
                {
                    if (m.Name == "/" || m.Name == "//" || m.Data.Length < 20) continue;
                    // A full PPC COFF object starts with the machine word; short
                    // imports (Sig1 = 0) do not -- only those carry an ordinal.
                    if (m.Data[0] == (ImageFileMachinePowerPcBe & 0xFF) &&
                        m.Data[1] == (ImageFileMachinePowerPcBe >> 8)) continue;
                    if (!TryParseShortImport(m.Data, out string sym, out string dll, out int ordinal, out int itype))
                        continue;
                    if (ordinal == 0) continue;
                    string module = dll.Split('@')[0].Trim();
                    if (module.Length == 0) ModuleNames.TryGetValue(baseName, out module);
                    if (string.IsNullOrEmpty(module)) continue;
                    if (!index.ContainsKey(sym))                     // setdefault: first wins
                        index[sym] = new Imp { Module = module, Ordinal = ordinal, IsVar = itype == 1 || itype == 2 };
                }
            }
            return index;
        }

        private struct Member { public string Name; public byte[] Data; }

        private static IEnumerable<Member> ReadArchiveMembers(byte[] blob)
        {
            // "!<arch>\n"
            byte[] magic = Encoding.ASCII.GetBytes("!<arch>\n");
            if (blob.Length < 8) yield break;
            for (int i = 0; i < 8; i++) if (blob[i] != magic[i]) yield break;
            int pos = 8, idx = 0;
            while (pos + 60 <= blob.Length)
            {
                string name = Encoding.ASCII.GetString(blob, pos, 16).TrimEnd();
                string sizeStr = Encoding.ASCII.GetString(blob, pos + 48, 10).Trim();
                if (!int.TryParse(sizeStr, out int size) || size < 0 || pos + 60 + size > blob.Length) yield break;
                var data = new byte[size];
                Array.Copy(blob, pos + 60, data, 0, size);
                // members[2] is the "//" longnames table; we resolve no long names
                // here (short-import members are named "/", data holds the strings).
                if (!(idx == 2 && name == "//"))
                    yield return new Member { Name = name, Data = data };
                pos += 60 + size + (size & 1);       // 2-byte aligned
                idx++;
            }
        }

        // IMPORT_OBJECT_HEADER: Sig1(2) Sig2(2) Version(2) Machine(2) TimeDate(4)
        // SizeOfData(4) OrdinalOrHint(2) TypeBits(2); then symbol\0 dll\0.
        private static bool TryParseShortImport(byte[] d, out string sym, out string dll, out int ordinal, out int itype)
        {
            sym = dll = null; ordinal = 0; itype = 0;
            if (d.Length < 20) return false;
            ordinal = d[16] | (d[17] << 8);
            int typebits = d[18] | (d[19] << 8);
            itype = typebits & 0x3;
            int p = 20, e = p;
            while (e < d.Length && d[e] != 0) e++;
            if (e >= d.Length) return false;
            sym = Encoding.ASCII.GetString(d, p, e - p);
            p = e + 1; e = p;
            while (e < d.Length && d[e] != 0) e++;
            dll = e <= d.Length ? Encoding.ASCII.GetString(d, p, Math.Min(e, d.Length) - p) : "";
            return true;
        }

        private static bool IsEmittableName(string name)
        {
            foreach (char c in name)
            {
                if (c > 0x7F) return false;
                if (!(char.IsLetterOrDigit(c) || c == '_' || c == '@' || c == '$' || c == '.')) return false;
            }
            return name.Length > 0;
        }

        private static string EmitAsm(SortedDictionary<string, Imp> index)
        {
            var sb = new StringBuilder();
            sb.Append("# RXDK-360 global kernel import library -- generated, do not edit.\n");
            sb.Append("# module_index is left 0; elf2xex patches it per the XEX name table.\n\n");
            foreach (var kv in index)                              // SortedDictionary(Ordinal): sorted by name
            {
                string name = kv.Key; Imp imp = kv.Value;
                if (string.IsNullOrEmpty(imp.Module) || imp.Ordinal == 0) continue;
                if (!IsEmittableName(name)) continue;
                if (!imp.IsVar)
                {
                    sb.Append("    .section .kthunks.").Append(name).Append(",\"axG\",@progbits,").Append(name).Append(",comdat\n");
                    sb.Append("    .globl ").Append(name).Append('\n');
                    sb.Append(name).Append(":\n");
                    sb.AppendFormat("    .long 0x{0:X8}, 0x{1:X8}, 0x7D6903A6, 0x4E800420\n",
                        0x01000000u | (uint)imp.Ordinal, 0x02000000u | (uint)imp.Ordinal);
                }
                sb.Append("    .section .kvars.").Append(name).Append(",\"awG\",@progbits,").Append(name).Append(",comdat\n");
                sb.Append("    .globl __imp_").Append(name).Append('\n');
                if (imp.IsVar)
                {
                    sb.Append("    .globl ").Append(name).Append('\n');
                    sb.Append("    .p2align 2\n");
                    sb.Append(name).Append(":\n");
                }
                sb.Append("__imp_").Append(name).Append(":\n");
                sb.AppendFormat("    .long 0x{0:X8}\n\n", (uint)imp.Ordinal);
            }
            return sb.ToString();
        }

        private static void RunTool(string exe, string args)
        {
            var psi = new ProcessStartInfo(exe, args)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
            };
            using (var p = Process.Start(psi))
            {
                string err = p.StandardError.ReadToEnd();
                p.StandardOutput.ReadToEnd();
                p.WaitForExit();
                if (p.ExitCode != 0)
                    throw new Exception("kernel_import build: " + Path.GetFileName(exe) + " failed: " + err);
            }
        }
    }
}
