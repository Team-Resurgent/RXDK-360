// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// Manifest-driven installer. The Xbox 360 XDK setup carries a manifest.csv that
// maps every payload file to a destination token and lists registry, shortcut
// and self-register actions. This engine replays those actions RELOCATED for
// RXDK-360 (XDK -> the chosen install dir; Start-menu group -> "RXDK-360"), so
// the result is a faithful install that can live side by side with a stock XDK.
//
// Columns:  lcid,arch,action,destToken,arg1,arg2,arg3,hash
//   file/copy : arg1 = <destToken>\relpath (source in staging), arg2 = flags (SO=self-register)
//   addreg    : arg1 = subkey, arg2 = value name, arg3 = data, hash = type (D=DWORD, else string)
//   shortcut  : arg1 = target relpath under destToken, arg2 = link name(.lnk), arg3 = description, ... = workdir
//
// Skipped in this pass: the vs71/vs80/vs90/vs100 rows (the OLD VS integration -
// RXDK-360 ships its own modern-VS platform), and cmd/pnpdrv/help sub-installers.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using Microsoft.Win32;

namespace Rxdk.Xdk.Unpacker
{
    internal sealed class ManifestInstaller
    {
        private readonly string _staging;   // where the cabs were extracted
        private readonly string _installDir; // XDK -> here (e.g. C:\Program Files\RXDK-360)
        private readonly string _group = "RXDK-360"; // Start-menu program group
        private readonly List<string> _undo = new List<string>();
        private int _files, _regs, _links, _selfreg, _skipped;
        public bool DryRun;   // resolve + count actions, but change nothing

        public ManifestInstaller(string stagingDir, string installDir)
        {
            _staging = stagingDir;
            _installDir = installDir;
        }

        public void Run(string undoLogPath)
        {
            string manifest = Path.Combine(_staging, "manifest.csv");
            if (!File.Exists(manifest))
                throw new FileNotFoundException("manifest.csv not found in " + _staging);

            foreach (var line in File.ReadAllLines(manifest))
            {
                if (line.Length == 0) continue;
                var f = line.Split(',');
                if (f.Length < 4) continue;
                string lcid = f[0], arch = f[1].ToLowerInvariant(), action = f[2].ToLowerInvariant(), tok = f[3];

                // locale: common + en-US only
                if (lcid != "0000" && lcid != "0409") { continue; }
                // skip the old VS integration rows (we ship our own)
                if (arch.StartsWith("vs")) { continue; }

                try
                {
                    switch (action)
                    {
                        case "file":  DoFile(tok, Get(f, 4), Get(f, 5)); break;
                        case "copy":  DoCopy(tok, Get(f, 4), Get(f, 5)); break;
                        case "addreg": DoAddReg(tok, Get(f, 4), Get(f, 5), Get(f, 6), Get(f, 7)); break;
                        case "shortcut": DoShortcut(tok, Get(f, 4), Get(f, 5), Get(f, 6), Get(f, 7)); break;
                        default: break; // remove/delreg/removedir/cmd/pnpdrv/nextcab/help3cmd -> ignore on install
                    }
                }
                catch (Exception ex)
                {
                    Console.Error.WriteLine("  ! " + action + " " + Get(f, 4) + " : " + ex.Message);
                    _skipped++;
                }
            }

            File.WriteAllLines(undoLogPath, _undo);
            Console.WriteLine("manifest: {0} files, {1} reg values, {2} shortcuts, {3} self-registered ({4} skipped)",
                _files, _regs, _links, _selfreg, _skipped);
        }

        private static string Get(string[] a, int i) { return i < a.Length ? a[i] : ""; }

        // ---- destination token -> real path -----------------------------------
        private string ResolveDir(string token)
        {
            string win = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            switch (token.ToUpperInvariant())
            {
                case "XDK": return _installDir;
                case "SYSTEM_DIR": return Path.Combine(win, "System32");   // x86 proc -> SysWOW64 (32-bit)
                case "SYSTEM64_DIR": return Path.Combine(win, "Sysnative"); // -> real System32 (64-bit)
                case "WINDOWS_DIR": return win;
                case "PROGRAMFILES":
                    return Environment.GetEnvironmentVariable("ProgramW6432")
                           ?? Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
                case "ALLUSERSCOMMONAPPDATA": return Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData);
                case "ALLUSERSCOMMONPROGRAMS": return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms), _group);
                case "CURRENTUSERCOMMONPROGRAMS": return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), _group);
                default: return null; // unknown token (VS*/HELP*) -> skip
            }
        }

        // Strip a leading "<TOKEN>\" from a manifest path so we get the relative part.
        private static string Rel(string p)
        {
            int i = p.IndexOf('\\');
            return i >= 0 ? p.Substring(i + 1) : p;
        }

        // ---- file / copy -------------------------------------------------------
        private void DoFile(string token, string srcRelWithToken, string flags)
        {
            string destBase = ResolveDir(token);
            if (destBase == null) { _skipped++; return; }
            string rel = Rel(srcRelWithToken);
            string src = Path.Combine(_staging, srcRelWithToken.Replace('/', '\\'));
            if (!File.Exists(src)) { _skipped++; return; }
            string dst = Path.Combine(destBase, rel.Replace('/', '\\'));
            bool so = flags.Trim().Equals("SO", StringComparison.Ordinal); // self-registering object
            _files++;
            if (so) _selfreg++;
            if (DryRun) return;

            // idempotent: skip the copy if an identical-size file is already there
            // (fast re-runs / repair), but still (re)register SO objects.
            if (!(File.Exists(dst) && new FileInfo(dst).Length == new FileInfo(src).Length))
            {
                Directory.CreateDirectory(Path.GetDirectoryName(dst));
                File.Copy(src, dst, true);
            }
            _undo.Add("file|" + dst);
            if (so)
            {
                bool win64 = string.Equals(token, "SYSTEM64_DIR", StringComparison.OrdinalIgnoreCase);
                SelfRegister(dst, win64, false);
                _undo.Add("selfreg|" + (win64 ? "64" : "32") + "|" + dst);
            }
        }

        // ---- copy (duplicate an already-staged file to another dest path) ------
        private void DoCopy(string token, string srcRelWithToken, string dstRelWithToken)
        {
            string destBase = ResolveDir(token);
            if (destBase == null || string.IsNullOrEmpty(dstRelWithToken)) { _skipped++; return; }
            string src = Path.Combine(_staging, srcRelWithToken.Replace('/', '\\'));
            if (!File.Exists(src)) { _skipped++; return; }
            string dst = Path.Combine(destBase, Rel(dstRelWithToken).Replace('/', '\\'));
            _files++;
            if (DryRun) return;
            if (!(File.Exists(dst) && new FileInfo(dst).Length == new FileInfo(src).Length))
            {
                Directory.CreateDirectory(Path.GetDirectoryName(dst));
                File.Copy(src, dst, true);
            }
            _undo.Add("file|" + dst);
        }

        private static void SelfRegister(string dll, bool win64, bool unregister)
        {
            string win = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            string regsvr = Path.Combine(win, win64 ? "Sysnative" : "System32", "regsvr32.exe");
            var psi = new ProcessStartInfo(regsvr, (unregister ? "/u /s \"" : "/s \"") + dll + "\"")
            { UseShellExecute = false, CreateNoWindow = true };
            using (var p = Process.Start(psi)) { p.WaitForExit(); }
        }

        // ---- registry ----------------------------------------------------------
        private void DoAddReg(string root, string subkey, string valueName, string data, string type)
        {
            // Preserve side-by-side: never rewrite the stock XDK identity keys.
            if (subkey.IndexOf("Xbox\\2.0\\SDK", StringComparison.OrdinalIgnoreCase) >= 0 ||
                subkey.IndexOf("XenonSDK", StringComparison.OrdinalIgnoreCase) >= 0)
            { _skipped++; return; }

            _regs++;
            if (DryRun) return;
            // Write the native 64-bit view: this x86 process is otherwise WOW64-
            // redirected to Wow6432Node, which is wrong for the 64-bit shell etc.
            RegistryHive hive = root.Equals("HKCU", StringComparison.OrdinalIgnoreCase)
                ? RegistryHive.CurrentUser : RegistryHive.LocalMachine;
            using (var baseKey = RegistryKey.OpenBaseKey(hive, RegistryView.Registry64))
            using (var k = baseKey.CreateSubKey(subkey))
            {
                if (k == null) { _skipped++; return; }
                if (!string.IsNullOrEmpty(valueName) || !string.IsNullOrEmpty(data))
                {
                    data = Expand(data);
                    if (type.Equals("D", StringComparison.OrdinalIgnoreCase))
                        k.SetValue(valueName, ParseDword(data), RegistryValueKind.DWord);
                    else
                        k.SetValue(valueName, data, RegistryValueKind.String);
                }
            }
            _undo.Add("reg|" + root + "|" + subkey + "|" + valueName);
        }

        private string Expand(string s)
        {
            if (string.IsNullOrEmpty(s)) return s;
            return s.Replace("%XDK%", _installDir + "\\")
                    .Replace("%SYSTEM_DIR%", ResolveDir("SYSTEM_DIR") + "\\");
        }

        private static int ParseDword(string s)
        {
            s = (s ?? "").Trim();
            if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                return Convert.ToInt32(s.Substring(2), 16);
            int v; return int.TryParse(s, out v) ? v : 0;
        }

        // ---- shortcuts ---------------------------------------------------------
        private void DoShortcut(string token, string targetRel, string linkName, string description, string workdir)
        {
            string targetBase = ResolveDir(token);
            if (targetBase == null || string.IsNullOrEmpty(linkName)) { _skipped++; return; }
            // shortcut target paths are already relative to the token (no prefix) -
            // unlike file rows, so do NOT strip a leading segment here.
            string target = Path.Combine(targetBase, targetRel.Replace('/', '\\'));
            string linkDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms), _group);
            string link = Path.Combine(linkDir, linkName.Replace('/', '\\'));
            _links++;
            if (DryRun) return;
            Directory.CreateDirectory(Path.GetDirectoryName(link));
            // workdir (optional) is a path relative to the same token
            string wd = string.IsNullOrEmpty(workdir) ? Path.GetDirectoryName(target)
                : Path.Combine(targetBase, workdir.Replace('/', '\\'));
            ShellLink.Create(link, target, null, wd, description, target, 0);
            _undo.Add("shortcut|" + link);
        }

        // ---- uninstall (reverse the undo log) ----------------------------------
        public static void Uninstall(string undoLogPath)
        {
            if (!File.Exists(undoLogPath)) return;
            var lines = File.ReadAllLines(undoLogPath);
            Array.Reverse(lines);
            foreach (var l in lines)
            {
                var f = l.Split('|');
                try
                {
                    switch (f[0])
                    {
                        case "file": if (File.Exists(f[1])) File.Delete(f[1]); break;
                        case "shortcut": if (File.Exists(f[1])) File.Delete(f[1]); break;
                        case "selfreg": SelfRegister(f[2], f[1] == "64", true); break;
                        case "reg":
                            RegistryHive hive = f[1].Equals("HKCU", StringComparison.OrdinalIgnoreCase)
                                ? RegistryHive.CurrentUser : RegistryHive.LocalMachine;
                            using (var baseKey = RegistryKey.OpenBaseKey(hive, RegistryView.Registry64))
                            using (var k = baseKey.OpenSubKey(f[2], true))
                                if (k != null && !string.IsNullOrEmpty(f[3])) { try { k.DeleteValue(f[3], false); } catch { } }
                            break;
                    }
                }
                catch { }
            }
        }
    }
}
