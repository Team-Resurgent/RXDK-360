// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Xml.Linq;
using System.Threading.Tasks;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Task = System.Threading.Tasks.Task;

namespace Rxdk360.Package.Services
{
    /// <summary>
    /// F5 for Xbox 360 + Copy to Hard Drive: build (which xbcp's via Deploy), then
    /// Debug Adapter Host → Rxdk.Xbox360.DebugAdapter.
    ///
    /// Copy to Hard Drive is the only F5 this package owns. Xenia and Emulate DVD
    /// fall through. RxdkXeniaPath is not a trigger — Deployment Type is.
    ///
    /// Source debugging is validated on Debug (legacy PDB / modern DWARF). Other
    /// configs still launch if they are Copy to Hard Drive.
    /// </summary>
    internal static class HardwareDebugLauncher
    {
        internal const string CopyToHardDrive = "CopyToHardDrive";
        internal const string EmulateDvd = "EmulateDvd";
        internal const string Xenia = "Xenia";

        private static TitleOutputPane _titlePane;

        internal sealed class StartupInfo
        {
            public EnvDTE.Project Project;
            public string ProjectPath;
            public string ProjectDir;
            public string SolutionConfig;
            public string ConfigName;
            public string PlatformName;
            public string DeploymentType;
            public string DebuggerFlavor;
            public string XeniaPath;
            public string ImagePath;
            public string RemoteRoot;
            public string RemoteMachine;
        }

        public static async Task<bool> IsCopyToHardDriveStartupAsync(AsyncPackage package)
        {
            try
            {
                var info = await GetStartupInfoAsync(package);
                return IsHardwareF5(info);
            }
            catch
            {
                return false;
            }
        }

        internal static bool IsXeniaFallthrough(StartupInfo info) =>
            info != null &&
            string.Equals(info.PlatformName, "Xbox 360", StringComparison.OrdinalIgnoreCase) &&
            string.Equals(info.DeploymentType, Xenia, StringComparison.OrdinalIgnoreCase);

        internal static bool IsHardwareF5(StartupInfo info)
        {
            if (info == null) return false;
            if (!string.Equals(info.PlatformName, "Xbox 360", StringComparison.OrdinalIgnoreCase))
                return false;
            if (string.Equals(info.DeploymentType, Xenia, StringComparison.OrdinalIgnoreCase))
                return false;
            if (string.Equals(info.DeploymentType, EmulateDvd, StringComparison.OrdinalIgnoreCase))
                return false;
            return string.Equals(info.DeploymentType, CopyToHardDrive, StringComparison.OrdinalIgnoreCase)
                || string.Equals(info.DebuggerFlavor, "Xbox360Debugger", StringComparison.OrdinalIgnoreCase)
                || string.IsNullOrEmpty(info.DeploymentType);
        }

        internal static Task<StartupInfo> GetStartupInfoForF5Async(AsyncPackage package, string hintVcxproj = null) =>
            GetStartupInfoAsync(package, hintVcxproj);

        private static EnvDTE.DTE GetDte(AsyncPackage package)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            try
            {
                return Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(EnvDTE._DTE)) as EnvDTE.DTE;
            }
            catch { return null; }
        }

        private static void F5Log(string message)
        {
            try
            {
                File.AppendAllText(
                    Path.Combine(Path.GetTempPath(), "rxdk360-f5.log"),
                    DateTime.Now.ToString("HH:mm:ss.fff") + " " + message + Environment.NewLine);
            }
            catch { }
        }

        private static bool IsOurs(StartupInfo info) => IsHardwareF5(info);

        public static async Task LaunchAsync(AsyncPackage package, bool noDebug, StartupInfo info = null)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            try
            {
                if (info == null)
                    info = await GetStartupInfoAsync(package);
            }
            catch (Exception ex)
            {
                await ShowMessageAsync(package, "Could not read the active Xbox 360 configuration: " + ex.Message);
                return;
            }
            if (!IsHardwareF5(info))
            {
                await ShowMessageAsync(package,
                    "F5 for Copy to Hard Drive could not start. Set Deployment Type to Copy to Hard Drive, save the project, and try again.");
                return;
            }

            var dap = ToolLocator.ResolveDap();
            if (string.IsNullOrEmpty(dap) || !File.Exists(dap))
            {
                await ShowAsync(package, "Rxdk.Xbox360.DebugAdapter.exe not found. Rebuild the RXDK-360 VSIX (tools\\) or set RXDK360_TOOLS_DIR.");
                return;
            }

            if (!await BuildProjectAsync(package, info))
            {
                await ShowAsync(package, "Build failed — see the Output / Error List.");
                return;
            }

            string xex = info.ImagePath;
            if (string.IsNullOrEmpty(xex) || !File.Exists(xex))
                xex = GuessXex(info);
            if (string.IsNullOrEmpty(xex) || !File.Exists(xex))
            {
                await ShowAsync(package, "No XEX at ImagePath. Build the Xbox 360 title, then F5.");
                return;
            }

            string name = Path.GetFileNameWithoutExtension(xex);
            string pdb = Path.ChangeExtension(xex, ".pdb");
            if (!File.Exists(pdb)) pdb = Path.ChangeExtension(xex, ".xdb");
            if (!File.Exists(pdb)) pdb = Path.ChangeExtension(xex, ".elf");

            var launch = new Dictionary<string, object>
            {
                ["$adapter"] = dap,
                ["type"] = "rxdk360",
                ["request"] = "launch",
                ["name"] = (noDebug ? "Run " : "Debug ") + name,
                ["program"] = xex,
                ["symbols"] = File.Exists(pdb) ? pdb : "",
                ["console"] = info.RemoteMachine ?? "",
                ["remoteDir"] = string.IsNullOrEmpty(info.RemoteRoot) ? "devkit:\\" + name : info.RemoteRoot,
                ["stopAtEntry"] = !noDebug,
                ["noDebug"] = noDebug,
                ["__titleOutputFile"] = Path.Combine(Path.GetTempPath(), "rxdk360-title-" + name + ".log"),
            };
            var launchFile = Path.Combine(Path.GetTempPath(), $"rxdk360-launch-{name}.json");
            File.WriteAllText(launchFile, SimpleJson(launch));

            _titlePane?.Stop();
            _titlePane = new TitleOutputPane(package);
            await _titlePane.StartAsync((string)launch["__titleOutputFile"]);

            var dte = GetDte(package);
            try
            {
                dte?.ExecuteCommand("DebugAdapterHost.Launch", $"/LaunchJson:\"{launchFile}\"");
            }
            catch (Exception ex)
            {
                await ShowAsync(package, "Failed to start debugging: " + ex.Message +
                    ". Is the Visual Studio Debug Adapter Host component installed?");
            }
        }

        private static async Task<bool> BuildProjectAsync(AsyncPackage package, StartupInfo info)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            string devenv;
            string vcxproj;
            string configuration;
            string platform;
            try
            {
                var dte = GetDte(package);
                if (dte == null)
                    return false;
                devenv = dte.FullName;
                vcxproj = !string.IsNullOrEmpty(info.ProjectPath) ? info.ProjectPath : info.Project?.FullName;
                if (string.IsNullOrEmpty(vcxproj))
                    return false;
                configuration = info.ConfigName ?? "Debug";
                platform = info.PlatformName ?? "Xbox 360";
            }
            catch (Exception ex)
            {
                await ShowAsync(package, "Build could not be started: " + ex.Message);
                return false;
            }

            // Do not use SolutionBuild.BuildProject — Xbox 360 solution configs are
            // "Debug|Xbox 360", and calling SolutionBuild from F5 raises
            // FindActiveProjectCfgName E_UNEXPECTED.
            return await Task.Run(() => RunMsbuild(devenv, vcxproj, configuration, platform));
        }

        private static bool RunMsbuild(string devenv, string vcxproj, string configuration, string platform)
        {
            string vsRoot = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(devenv) ?? "", "..", ".."));
            string msbuild = Path.Combine(vsRoot, "MSBuild", "Current", "Bin", "MSBuild.exe");
            if (!File.Exists(msbuild))
                msbuild = Path.Combine(vsRoot, "MSBuild", "Current", "Bin", "amd64", "MSBuild.exe");
            if (!File.Exists(msbuild))
                return false;

            var psi = new ProcessStartInfo
            {
                FileName = msbuild,
                Arguments = "\"" + vcxproj + "\" /nologo /m /v:m"
                    + " /p:Configuration=\"" + configuration + "\""
                    + " /p:Platform=\"" + platform + "\"",
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                WorkingDirectory = Path.GetDirectoryName(vcxproj) ?? Environment.CurrentDirectory,
            };
            var log = new StringBuilder();
            using (var p = Process.Start(psi))
            {
                if (p == null) return false;
                p.OutputDataReceived += (s, e) => { if (e.Data != null) log.AppendLine(e.Data); };
                p.ErrorDataReceived += (s, e) => { if (e.Data != null) log.AppendLine(e.Data); };
                p.BeginOutputReadLine();
                p.BeginErrorReadLine();
                p.WaitForExit();
                if (p.ExitCode != 0)
                {
                    try { File.WriteAllText(Path.Combine(Path.GetTempPath(), "rxdk360-msbuild.log"), log.ToString()); }
                    catch { }
                    return false;
                }
                return true;
            }
        }

        private static async Task<StartupInfo> GetStartupInfoAsync(AsyncPackage package, string hintVcxproj = null)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            try
            {
                return GetStartupInfoCore(package, hintVcxproj);
            }
            catch (Exception ex)
            {
                F5Log("GetStartupInfo " + ex.GetType().Name + ": " + ex.Message);
                return FromVcxproj(hintVcxproj, null);
            }
        }

        private static StartupInfo GetStartupInfoCore(AsyncPackage package, string hintVcxproj)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            IVsHierarchy hier = null;
            EnvDTE.Project proj = null;
            EnvDTE.DTE dte = GetDte(package);
            F5Log("dte=" + (dte != null) + " hint=" + (hintVcxproj ?? ""));

            try
            {
                var sbm = Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(SVsSolutionBuildManager)) as IVsSolutionBuildManager;
                if (sbm != null &&
                    sbm.get_StartupProject(out hier) == VSConstants.S_OK &&
                    hier != null)
                    proj = GetExtObject(hier) as EnvDTE.Project;
            }
            catch (Exception ex) { F5Log("get_StartupProject " + ex.Message); }

            if (proj == null && dte != null)
            {
                try { proj = FindStartupProject(dte); }
                catch (Exception ex) { F5Log("FindStartupProject " + ex.Message); }
            }

            if (proj == null && dte != null && !string.IsNullOrEmpty(hintVcxproj))
                proj = FindProjectByName(dte, hintVcxproj);

            if (proj == null)
            {
                F5Log("no DTE project; file fallback");
                return FromVcxproj(hintVcxproj, null);
            }

            F5Log("project=" + proj.FullName);

            if (hier == null)
            {
                try
                {
                    var sol = Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(SVsSolution)) as IVsSolution;
                    if (sol != null && !string.IsNullOrEmpty(proj.UniqueName) &&
                        sol.GetProjectOfUniqueName(proj.UniqueName, out hier) != VSConstants.S_OK)
                        hier = null;
                }
                catch { hier = null; }
            }

            string projectDir;
            try { projectDir = Path.GetDirectoryName(proj.FullName); }
            catch { return FromVcxproj(hintVcxproj ?? proj.FullName, proj); }

            string configName = "Debug";
            string platformName = "Xbox 360";
            string fullConfig = "Debug|Xbox 360";
            try
            {
                var cfg = proj.ConfigurationManager?.ActiveConfiguration;
                if (cfg != null)
                {
                    configName = cfg.ConfigurationName;
                    platformName = cfg.PlatformName ?? platformName;
                    fullConfig = cfg.ConfigurationName + "|" + platformName;
                }
            }
            catch { }

            var bps = hier as IVsBuildPropertyStorage;
            string vcx = proj.FullName;

            string deploy = NormalizeDeploy(
                VcEvaluate(proj, "$(DeploymentType)")
                ?? VcRuleValue(proj, "Xbox360Deploy", "DeploymentType")
                ?? ReadProp(bps, "DeploymentType", fullConfig)
                ?? ReadProjectPropertyFromVcxproj(vcx, configName, platformName, "DeploymentType")
                ?? ReadDeployTypeFromVcxproj(vcx, configName, platformName));

            string flavor = FirstNonEmpty(
                VcEvaluate(proj, "$(DebuggerFlavor)"),
                ReadProp(bps, "DebuggerFlavor", fullConfig));

            string xenia = FirstNonEmpty(
                VcEvaluate(proj, "$(RxdkXeniaPath)"),
                ReadProp(bps, "RxdkXeniaPath", fullConfig),
                ReadProjectPropertyFromVcxproj(vcx, configName, platformName, "RxdkXeniaPath"));

            string image = ExpandPath(projectDir,
                VcEvaluate(proj, "$(ImagePath)")
                ?? ReadProp(bps, "ImagePath", fullConfig)
                ?? ReadProp(bps, "ImageXexOutput", fullConfig));

            F5Log("config=" + fullConfig + " deploy=" + (deploy ?? "") + " flavor=" + (flavor ?? ""));

            return new StartupInfo
            {
                Project = proj,
                ProjectPath = vcx,
                ProjectDir = projectDir,
                SolutionConfig = configName,
                ConfigName = configName,
                PlatformName = platformName,
                DeploymentType = deploy,
                DebuggerFlavor = flavor,
                XeniaPath = xenia,
                ImagePath = image,
                RemoteRoot = ExpandPath(projectDir,
                    VcEvaluate(proj, "$(RemoteRoot)")
                    ?? ReadProp(bps, "RemoteRoot", fullConfig)),
                RemoteMachine = FirstNonEmpty(
                    VcEvaluate(proj, "$(RemoteMachine)"),
                    VcEvaluate(proj, "$(DefaultConsole)"),
                    ReadProp(bps, "RemoteMachine", fullConfig),
                    ReadProp(bps, "DefaultConsole", fullConfig)),
            };
        }

        private static StartupInfo FromVcxproj(string vcxproj, EnvDTE.Project proj)
        {
            if (string.IsNullOrEmpty(vcxproj) || !File.Exists(vcxproj))
            {
                F5Log("FromVcxproj missing " + (vcxproj ?? "(null)"));
                return null;
            }
            string dir = Path.GetDirectoryName(vcxproj);
            string config = "Debug";
            string platform = "Xbox 360";
            string deploy = NormalizeDeploy(
                ReadProjectPropertyFromVcxproj(vcxproj, config, platform, "DeploymentType")
                ?? ReadDeployTypeFromVcxproj(vcxproj, config, platform));
            F5Log("FromVcxproj deploy=" + (deploy ?? "") + " " + vcxproj);
            return new StartupInfo
            {
                Project = proj,
                ProjectPath = vcxproj,
                ProjectDir = dir,
                SolutionConfig = config,
                ConfigName = config,
                PlatformName = platform,
                DeploymentType = deploy,
                XeniaPath = ReadProjectPropertyFromVcxproj(vcxproj, config, platform, "RxdkXeniaPath"),
            };
        }

        private static EnvDTE.Project FindStartupProject(EnvDTE.DTE dte)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (dte?.Solution == null) return null;

            try
            {
                object names = dte.Solution.SolutionBuild?.StartupProjects;
                foreach (string name in EnumerateStrings(names))
                {
                    EnvDTE.Project hit = FindProjectByName(dte, name);
                    if (hit != null) return hit;
                }
            }
            catch { }

            try
            {
                EnvDTE.Project selected = FirstProject(dte.ActiveSolutionProjects);
                if (selected != null) return selected;
            }
            catch { }

            EnvDTE.Project first360 = null;
            EnvDTE.Project firstVcx = null;
            int count = 0;
            foreach (EnvDTE.Project p in EnumerateProjects(dte))
            {
                count++;
                if (firstVcx == null && IsVcxproj(p)) firstVcx = p;
                if (IsXbox360Project(p))
                {
                    if (first360 == null) first360 = p;
                }
            }
            if (first360 != null) return first360;
            if (count == 1) return firstVcx;
            return firstVcx;
        }

        private static EnvDTE.Project FindProjectByName(EnvDTE.DTE dte, string name)
        {
            if (string.IsNullOrEmpty(name)) return null;
            foreach (EnvDTE.Project p in EnumerateProjects(dte))
            {
                try
                {
                    if (string.Equals(p.UniqueName, name, StringComparison.OrdinalIgnoreCase) ||
                        string.Equals(p.FullName, name, StringComparison.OrdinalIgnoreCase) ||
                        string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase) ||
                        (!string.IsNullOrEmpty(p.FullName) &&
                         p.FullName.EndsWith(name, StringComparison.OrdinalIgnoreCase)))
                        return p;
                }
                catch { }
            }
            return null;
        }

        private static IEnumerable<EnvDTE.Project> EnumerateProjects(EnvDTE.DTE dte)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (dte?.Solution?.Projects == null) yield break;
            foreach (EnvDTE.Project p in dte.Solution.Projects)
            {
                foreach (var child in EnumerateProjectTree(p))
                    yield return child;
            }
        }

        private static IEnumerable<EnvDTE.Project> EnumerateProjectTree(EnvDTE.Project p)
        {
            if (p == null) yield break;
            yield return p;
            EnvDTE.ProjectItems items = null;
            try { items = p.ProjectItems; }
            catch { }
            if (items == null) yield break;
            foreach (EnvDTE.ProjectItem item in items)
            {
                EnvDTE.Project sub = null;
                try { sub = item.SubProject; }
                catch { }
                if (sub == null) continue;
                foreach (var child in EnumerateProjectTree(sub))
                    yield return child;
            }
        }

        private static EnvDTE.Project FirstProject(object com)
        {
            if (com == null) return null;
            if (com is EnvDTE.Project p) return p;
            if (com is Array arr && arr.Length > 0)
                return arr.GetValue(arr.GetLowerBound(0)) as EnvDTE.Project;
            if (com is IEnumerable en)
            {
                foreach (object o in en)
                    return o as EnvDTE.Project;
            }
            return null;
        }

        private static IEnumerable<string> EnumerateStrings(object com)
        {
            if (com == null) yield break;
            if (com is string s)
            {
                yield return s;
                yield break;
            }
            if (com is Array arr)
            {
                foreach (object o in arr)
                    if (o is string t) yield return t;
                yield break;
            }
            if (com is IEnumerable en)
            {
                foreach (object o in en)
                    if (o is string t) yield return t;
            }
        }

        private static bool IsVcxproj(EnvDTE.Project p)
        {
            try
            {
                string fn = p.FullName;
                return !string.IsNullOrEmpty(fn) &&
                       fn.EndsWith(".vcxproj", StringComparison.OrdinalIgnoreCase);
            }
            catch { return false; }
        }

        private static bool IsXbox360Project(EnvDTE.Project p)
        {
            if (!IsVcxproj(p)) return false;
            try
            {
                string text = File.ReadAllText(p.FullName);
                return text.IndexOf("|Xbox 360", StringComparison.OrdinalIgnoreCase) >= 0
                    || text.IndexOf("'Xbox 360'", StringComparison.OrdinalIgnoreCase) >= 0;
            }
            catch { return false; }
        }

        private static string GuessXex(StartupInfo info)
        {
            if (info == null || string.IsNullOrEmpty(info.ProjectDir)) return null;
            string name = Path.GetFileNameWithoutExtension(
                !string.IsNullOrEmpty(info.ProjectPath) ? info.ProjectPath : (info.Project?.FullName ?? ""));
            if (string.IsNullOrEmpty(name)) return null;
            string cfg = string.IsNullOrEmpty(info.ConfigName) ? "Debug" : info.ConfigName;
            string guess = Path.Combine(info.ProjectDir, cfg, name + ".xex");
            return File.Exists(guess) ? guess : null;
        }

        /// <summary>Take the first token; drop leftover MSBuild macros.</summary>
        private static string NormalizeDeploy(string value)
        {
            if (string.IsNullOrWhiteSpace(value)) return null;
            string s = value.Trim();
            int semi = s.IndexOf(';');
            if (semi >= 0) s = s.Substring(0, semi).Trim();
            if (s.IndexOf("$(", StringComparison.Ordinal) >= 0) return null;
            if (s.Equals(CopyToHardDrive, StringComparison.OrdinalIgnoreCase) ||
                s.Equals(EmulateDvd, StringComparison.OrdinalIgnoreCase) ||
                s.Equals(Xenia, StringComparison.OrdinalIgnoreCase))
                return s;
            return null;
        }

        private static string ExpandPath(string projectDir, string image)
        {
            if (string.IsNullOrWhiteSpace(image)) return null;
            if (image.IndexOf("$(", StringComparison.Ordinal) >= 0) return null;
            if (!Path.IsPathRooted(image) && !string.IsNullOrEmpty(projectDir))
                return Path.GetFullPath(Path.Combine(projectDir, image));
            return image;
        }

        private static string FirstNonEmpty(params string[] values)
        {
            foreach (var v in values)
                if (!string.IsNullOrWhiteSpace(v) && v.IndexOf("$(", StringComparison.Ordinal) < 0)
                    return v.Trim();
            return null;
        }

        private static string VcEvaluate(EnvDTE.Project proj, string expression)
        {
            try
            {
                object cfg = ActiveVcConfig(proj);
                if (cfg == null) return null;
                object result = cfg.GetType().InvokeMember("Evaluate",
                    BindingFlags.InvokeMethod, null, cfg, new object[] { expression });
                string s = result as string;
                if (string.IsNullOrWhiteSpace(s) || s == expression) return null;
                return s.Trim();
            }
            catch { return null; }
        }

        private static string VcRuleValue(EnvDTE.Project proj, string rule, string property)
        {
            try
            {
                object cfg = ActiveVcConfig(proj);
                if (cfg == null) return null;
                object rules = cfg.GetType().InvokeMember("Rules", BindingFlags.GetProperty, null, cfg, null);
                if (rules == null) return null;
                object r = rules.GetType().InvokeMember("Item", BindingFlags.InvokeMethod, null, rules, new object[] { rule });
                if (r == null) return null;
                object val = r.GetType().InvokeMember("GetEvaluatedPropertyValue",
                    BindingFlags.InvokeMethod, null, r, new object[] { property });
                string s = val as string;
                return string.IsNullOrWhiteSpace(s) ? null : s.Trim();
            }
            catch { return null; }
        }

        private static object ActiveVcConfig(EnvDTE.Project proj)
        {
            object vc = proj?.Object;
            if (vc == null) return null;
            return vc.GetType().InvokeMember("ActiveConfiguration", BindingFlags.GetProperty, null, vc, null);
        }

        private static string ReadProjectPropertyFromVcxproj(string vcxproj, string config, string platform, string name)
        {
            if (string.IsNullOrEmpty(vcxproj) || !File.Exists(vcxproj)) return null;
            try
            {
                XDocument doc = XDocument.Load(vcxproj);
                string found = null;
                foreach (var group in doc.Root.Elements())
                {
                    if (group.Name.LocalName != "PropertyGroup") continue;
                    string cond = (string)group.Attribute("Condition");
                    if (!MsbuildConditionApplies(cond, config, platform)) continue;
                    foreach (var el in group.Elements())
                    {
                        if (el.Name.LocalName != name) continue;
                        string v = (el.Value ?? "").Trim();
                        if (v.Length != 0) found = v;
                    }
                }
                return found;
            }
            catch { return null; }
        }

        /// <summary>
        /// Last-resort: DeploymentType on a Deploy item / ItemDefinition whose Condition
        /// matches the active config. Does not invent the props default (EmulateDvd).
        /// </summary>
        private static string ReadDeployTypeFromVcxproj(string vcxproj, string config, string platform)
        {
            if (string.IsNullOrEmpty(vcxproj) || !File.Exists(vcxproj)) return null;
            try
            {
                XDocument doc = XDocument.Load(vcxproj);
                string found = null;
                foreach (var group in doc.Root.Elements())
                {
                    string local = group.Name.LocalName;
                    if (local != "ItemDefinitionGroup" && local != "ItemGroup") continue;
                    string cond = (string)group.Attribute("Condition");
                    if (!MsbuildConditionApplies(cond, config, platform)) continue;
                    foreach (var deploy in group.Elements())
                    {
                        if (deploy.Name.LocalName != "Deploy") continue;
                        string itemCond = (string)deploy.Attribute("Condition");
                        if (!MsbuildConditionApplies(itemCond, config, platform)) continue;
                        foreach (var meta in deploy.Elements())
                        {
                            if (meta.Name.LocalName != "DeploymentType") continue;
                            string metaCond = (string)meta.Attribute("Condition");
                            if (!MsbuildConditionApplies(metaCond, config, platform)) continue;
                            string v = (meta.Value ?? "").Trim();
                            if (v.Length != 0) found = v; // later groups win
                        }
                    }
                }
                return found;
            }
            catch { return null; }
        }

        private static bool MsbuildConditionApplies(string condition, string config, string platform)
        {
            if (string.IsNullOrWhiteSpace(condition)) return true;
            bool saw = false;
            bool hasConfig = ContainsMacro(condition, "Configuration");
            bool hasPlatform = ContainsMacro(condition, "Platform");
            if (hasConfig && hasPlatform && condition.IndexOf('|') >= 0)
            {
                saw = true;
                if (condition.IndexOf(config + "|" + platform, StringComparison.OrdinalIgnoreCase) < 0)
                    return false;
            }
            else
            {
                if (hasConfig)
                {
                    saw = true;
                    if (!QuotedEquals(condition, config)) return false;
                }
                if (hasPlatform)
                {
                    saw = true;
                    if (!QuotedEquals(condition, platform)) return false;
                }
            }
            return saw || !hasConfig && !hasPlatform;
        }

        private static bool ContainsMacro(string condition, string name) =>
            condition.IndexOf("$(" + name + ")", StringComparison.OrdinalIgnoreCase) >= 0;

        private static bool QuotedEquals(string condition, string value) =>
            condition.IndexOf("'" + value + "'", StringComparison.OrdinalIgnoreCase) >= 0 ||
            condition.IndexOf("\"" + value + "\"", StringComparison.OrdinalIgnoreCase) >= 0;

        private static string ReadProp(IVsBuildPropertyStorage bps, string name, string config)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (bps == null) return null;
            try
            {
                if (bps.GetPropertyValue(name, config, (uint)_PersistStorageType.PST_PROJECT_FILE, out string value) == VSConstants.S_OK
                    && !string.IsNullOrEmpty(value))
                    return value;
            }
            catch { }
            return null;
        }

        private static object GetExtObject(IVsHierarchy hier)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            return hier.GetProperty(VSConstants.VSITEMID_ROOT, (int)__VSHPROPID.VSHPROPID_ExtObject, out object ext) == VSConstants.S_OK
                ? ext : null;
        }

        internal static Task ShowMessageAsync(AsyncPackage package, string message) =>
            ShowAsync(package, message);

        private static async Task ShowAsync(AsyncPackage package, string message)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            VsShellUtilities.ShowMessageBox(package, message, "RXDK-360",
                OLEMSGICON.OLEMSGICON_INFO, OLEMSGBUTTON.OLEMSGBUTTON_OK, OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
        }

        private static string SimpleJson(Dictionary<string, object> map)
        {
            var sb = new System.Text.StringBuilder();
            sb.Append("{\n");
            bool first = true;
            foreach (var kv in map)
            {
                if (!first) sb.Append(",\n");
                first = false;
                sb.Append("  \"").Append(kv.Key).Append("\": ");
                if (kv.Value is bool b) sb.Append(b ? "true" : "false");
                else sb.Append('"').Append(kv.Value.ToString().Replace("\\", "\\\\").Replace("\"", "\\\"")).Append('"');
            }
            sb.Append("\n}\n");
            return sb.ToString();
        }
    }
}
