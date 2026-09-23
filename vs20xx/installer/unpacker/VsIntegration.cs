// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// Visual Studio integration: copy prebuilt net472 task DLLs + the Xbox 360
// platform into each VS, then install the packaged VSIX. Nothing is compiled
// here — task DLLs and the VSIX are produced when Setup (or the git tree) is
// packed.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Security.Principal;
using System.Text;

namespace Rxdk.Xdk.Unpacker
{
    internal static class VsIntegration
    {
        private static readonly string[] ToolsetDirs = { "v170", "v180" };
        private const string VsixId = "Rxdk360.Vsix.add38e43-cc73-4417-9c4b-e2d43131ab14";
        private static string _logPath;

        public static int Run(bool uninstall, string[] args)
        {
            bool skipVsix = false, skipPlatform = false;
            string root = null;
            for (int i = 0; i < args.Length; i++)
            {
                if (args[i].Equals("--skip-vsix", StringComparison.OrdinalIgnoreCase)) skipVsix = true;
                else if (args[i].Equals("--skip-platform", StringComparison.OrdinalIgnoreCase)) skipPlatform = true;
                else if (root == null) root = args[i];
                else return Usage();
            }
            if (string.IsNullOrEmpty(root)) return Usage();
            root = Path.GetFullPath(root);
            if (!uninstall && !Directory.Exists(root))
                throw new DirectoryNotFoundException("VS integration tree not found: " + root);

            _logPath = Path.Combine(
                Directory.Exists(root) ? root : Path.GetTempPath(),
                uninstall ? "vsuninstall.log" : "vsinstall.log");
            try { File.WriteAllText(_logPath, ""); } catch { }

            try
            {
                if (!IsAdmin()) return RelaunchElevated();

                var vsInstalls = GetVsInstalls();
                if (vsInstalls.Count == 0)
                    throw new InvalidOperationException("no Visual Studio 2022/2026 install found");

                string vsix = FirstExisting(
                    Path.Combine(root, "Rxdk360.Vsix.vsix"),
                    Path.Combine(root, "extension", "Rxdk360.Vsix", "bin", "Release", "Rxdk360.Vsix.vsix"));
                if (!uninstall && vsix == null)
                    throw new FileNotFoundException("packaged VSIX not found under " + root);

                if (uninstall)
                {
                    if (!skipPlatform)
                        DoPlatform(true, vsInstalls);
                    if (!skipVsix)
                        DoVsix(true, vsix, vsInstalls);
                }
                else
                {
                    // VSIXInstaller unpacks the payload (platform + task DLLs + DAP).
                    // C++ still loads platforms from VC\<toolset>\Platforms, so copy
                    // from the installed extension folder — do not unzip the .vsix.
                    if (!skipVsix)
                        DoVsix(false, vsix, vsInstalls);
                    if (!skipPlatform)
                        DoPlatform(false, vsInstalls);
                }

                if (!uninstall)
                {
                    Log("Done. New Project -> 'Xbox 360 Title' / 'Xbox 360 Static Library',");
                    Log("or set <Platform>Xbox 360</Platform> + <PlatformToolset>2010-01</PlatformToolset>.");
                }
                return 0;
            }
            catch (Exception ex)
            {
                Log("error: " + ex.Message);
                throw;
            }
        }

        private static int Usage()
        {
            Console.Error.WriteLine("  RxdkXdkUnpacker vsinstall   <vs20xx|vsintegrationDir> [--skip-vsix] [--skip-platform]");
            Console.Error.WriteLine("  RxdkXdkUnpacker vsuninstall <vs20xx|vsintegrationDir>");
            return 2;
        }

        private static void DoPlatform(bool uninstall, List<VsInstall> vsInstalls)
        {
            foreach (var vs in vsInstalls)
            {
                foreach (var ts in ToolsetDirs)
                {
                    string platRoot = Path.Combine(vs.Path, "MSBuild", "Microsoft", "VC", ts, "Platforms");
                    if (!Directory.Exists(platRoot)) continue;
                    string dst = Path.Combine(platRoot, "Xbox 360");
                    string legacyDst = Path.Combine(platRoot, "RXDK-360");
                    if (Directory.Exists(legacyDst))
                    {
                        DeleteTree(legacyDst);
                        Log("removed legacy " + legacyDst);
                    }
                    if (uninstall)
                    {
                        if (Directory.Exists(dst))
                        {
                            DeleteTree(dst);
                            Log("removed   " + dst);
                        }
                        string vcDll = Path.Combine(vs.Path, "MSBuild", "Microsoft", "VC", ts, "Rxdk.Xbox360.Build.dll");
                        if (File.Exists(vcDll))
                        {
                            File.Delete(vcDll);
                            Log("removed   " + vcDll);
                        }
                        continue;
                    }

                    string msbuild = FindInstalledMsbuildPayload(vs.Path);
                    if (msbuild == null)
                        throw new DirectoryNotFoundException(
                            "VSIX installed but MSBuild payload not found under " + vs.Path + " (is MSBuild\\Xbox 360 packed in the VSIX?)");
                    DeployPlatformFromExtension(msbuild, ts, dst);
                    Log("installed " + dst);
                }
            }
        }

        // VSIXInstaller already unpacked these next to the extension.
        private static string FindInstalledMsbuildPayload(string vsInstall)
        {
            var roots = new List<string>();
            roots.Add(Path.Combine(vsInstall, "Common7", "IDE", "Extensions"));
            string localVs = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Microsoft", "VisualStudio");
            if (Directory.Exists(localVs))
            {
                foreach (var dir in Directory.GetDirectories(localVs))
                {
                    string ext = Path.Combine(dir, "Extensions");
                    if (Directory.Exists(ext)) roots.Add(ext);
                }
            }
            foreach (var root in roots)
            {
                if (!Directory.Exists(root)) continue;
                string[] hits;
                try { hits = Directory.GetFiles(root, "Platform.props", SearchOption.AllDirectories); }
                catch { continue; }
                foreach (var props in hits)
                {
                    string xboxDir = Path.GetDirectoryName(props);
                    string msbuildDir = xboxDir != null ? Path.GetDirectoryName(xboxDir) : null;
                    if (xboxDir == null || msbuildDir == null) continue;
                    if (!Path.GetFileName(xboxDir).Equals("Xbox 360", StringComparison.OrdinalIgnoreCase)) continue;
                    if (!Path.GetFileName(msbuildDir).Equals("MSBuild", StringComparison.OrdinalIgnoreCase)) continue;
                    return msbuildDir;
                }
            }
            return null;
        }

        private static void DeployPlatformFromExtension(string msbuildPayload, string toolset, string dst)
        {
            string srcPlat = Path.Combine(msbuildPayload, "Xbox 360");
            string stockDll = Path.Combine(msbuildPayload, "tasks", toolset, "Rxdk.Xbox360.Build.dll");
            string modernDll = Path.Combine(msbuildPayload, "tasks", "Rxdk.Xbox360.Clang.Build.dll");
            if (!Directory.Exists(srcPlat))
                throw new DirectoryNotFoundException("extension is missing MSBuild\\Xbox 360");
            if (!File.Exists(stockDll))
                throw new FileNotFoundException("extension is missing " + stockDll);

            if (Directory.Exists(dst)) DeleteTree(dst);
            CopyTree(srcPlat, dst);
            File.Copy(stockDll, Path.Combine(dst, "Rxdk.Xbox360.Build.dll"), true);
            // Sibling of Microsoft.Build.CPPTasks.Common so MSBuild 18 can LoadFrom in-proc.
            string vcDir = Path.GetDirectoryName(Path.GetDirectoryName(dst));
            if (!string.IsNullOrEmpty(vcDir))
                File.Copy(stockDll, Path.Combine(vcDir, "Rxdk.Xbox360.Build.dll"), true);
            if (File.Exists(modernDll))
                File.Copy(modernDll, Path.Combine(dst, "Rxdk.Xbox360.Clang.Build.dll"), true);
        }

        private static void CopyTree(string src, string dst)
        {
            Directory.CreateDirectory(dst);
            foreach (var file in Directory.GetFiles(src))
                File.Copy(file, Path.Combine(dst, Path.GetFileName(file)), true);
            foreach (var dir in Directory.GetDirectories(src))
                CopyTree(dir, Path.Combine(dst, Path.GetFileName(dir)));
        }

        private static string VsixInstallerArgs(string instanceId)
        {
            string args = "/quiet /admin";
            if (!string.IsNullOrEmpty(instanceId))
                args += " /instanceIds:" + instanceId;
            return args;
        }

        private static void DoVsix(bool uninstall, string vsix, List<VsInstall> vsInstalls)
        {
            if (!uninstall && string.IsNullOrEmpty(vsix))
                throw new FileNotFoundException("packaged VSIX not found");

            foreach (var vs in vsInstalls)
            {
                string vsixInstaller = Path.Combine(vs.Path, "Common7", "IDE", "VSIXInstaller.exe");
                if (!File.Exists(vsixInstaller)) continue;
                string args = VsixInstallerArgs(vs.Id);
                for (int i = 0; i < 15; i++)
                {
                    int rc = RunHidden(vsixInstaller, args + " /uninstall:" + VsixId);
                    if (rc != 0) break;
                }
                if (uninstall)
                {
                    Log("uninstalled VSIX from " + vs.Path);
                    continue;
                }
                Log("installing VSIX into " + vs.Path + " ...");
                int irc = RunHidden(vsixInstaller, args + " \"" + vsix + "\"");
                if (irc != 0) throw new InvalidOperationException("VSIX install failed for " + vs.Path);
                string devenv = Path.Combine(vs.Path, "Common7", "IDE", "devenv.exe");
                if (File.Exists(devenv))
                    RunHidden(devenv, "/updateconfiguration");
            }
        }

        private static string FirstExisting(params string[] paths)
        {
            foreach (var p in paths)
                if (!string.IsNullOrEmpty(p) && File.Exists(p)) return p;
            return null;
        }

        private sealed class VsInstall
        {
            public string Path;
            public string Id;
        }

        private static List<string> VswhereProperty(string vswhere, string prop)
        {
            var psi = new ProcessStartInfo(vswhere,
                "-all -prerelease -products * -version [17.0,19.0) -property " + prop)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            using (var p = Process.Start(psi))
            {
                string o = p.StandardOutput.ReadToEnd();
                p.WaitForExit();
                var list = new List<string>();
                foreach (var line in o.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
                {
                    string s = line.Trim();
                    if (s.Length > 0) list.Add(s);
                }
                return list;
            }
        }

        private static void AddVsInstall(List<VsInstall> list, string path, string id)
        {
            if (string.IsNullOrEmpty(path) || !Directory.Exists(Path.Combine(path, "Common7", "IDE")))
                return;
            path = path.TrimEnd('\\', '/');
            foreach (var existing in list)
            {
                if (existing.Path.Equals(path, StringComparison.OrdinalIgnoreCase))
                    return;
            }
            list.Add(new VsInstall { Path = path, Id = id ?? "" });
        }

        private static void ScanVsYearFolder(List<VsInstall> list, string yearFolder)
        {
            if (!Directory.Exists(yearFolder)) return;
            foreach (var dir in Directory.GetDirectories(yearFolder))
                AddVsInstall(list, dir, "");
        }

        private static List<VsInstall> GetVsInstalls()
        {
            var list = new List<VsInstall>();
            string vswhere = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                "Microsoft Visual Studio", "Installer", "vswhere.exe");
            if (File.Exists(vswhere))
            {
                var paths = VswhereProperty(vswhere, "installationPath");
                var ids = VswhereProperty(vswhere, "instanceId");
                for (int i = 0; i < paths.Count; i++)
                    AddVsInstall(list, paths[i], i < ids.Count ? ids[i] : "");
            }

            string pf = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
            ScanVsYearFolder(list, Path.Combine(pf, "Microsoft Visual Studio", "2022"));
            ScanVsYearFolder(list, Path.Combine(pf, "Microsoft Visual Studio", "18"));
            ScanVsYearFolder(list, Path.Combine(pf, "Microsoft Visual Studio", "2026"));
            return list;
        }

        private static int RunHidden(string file, string arguments)
        {
            var psi = new ProcessStartInfo(file, arguments)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden,
            };
            using (var p = Process.Start(psi))
            {
                if (p == null) throw new InvalidOperationException("failed to start " + file);
                p.WaitForExit();
                return p.ExitCode;
            }
        }

        private static void DeleteTree(string path)
        {
            if (!Directory.Exists(path)) return;
            foreach (var f in Directory.GetFiles(path, "*", SearchOption.AllDirectories))
            {
                File.SetAttributes(f, FileAttributes.Normal);
                File.Delete(f);
            }
            Directory.Delete(path, true);
        }

        private static void Log(string s)
        {
            Report.Line(s);
            if (_logPath == null) return;
            try { File.AppendAllText(_logPath, s + Environment.NewLine); } catch { }
        }

        private static bool IsAdmin()
        {
            using (var id = WindowsIdentity.GetCurrent())
                return new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);
        }

        private static int RelaunchElevated()
        {
            Log("elevating...");
            var argv = Environment.GetCommandLineArgs();
            var sb = new StringBuilder();
            for (int i = 1; i < argv.Length; i++)
            {
                if (sb.Length > 0) sb.Append(' ');
                sb.Append(Quote(argv[i]));
            }
            var psi = new ProcessStartInfo
            {
                FileName = argv[0],
                Arguments = sb.ToString(),
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden,
            };
            using (var p = Process.Start(psi))
            {
                if (p == null) return 1;
                p.WaitForExit();
                return p.ExitCode;
            }
        }

        private static string Quote(string s)
        {
            if (s.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return s;
            return "\"" + s.Replace("\"", "\\\"") + "\"";
        }
    }
}
