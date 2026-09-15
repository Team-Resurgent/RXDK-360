// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// Xbox 360 post-link tools (imagexex, deploy), recompiled from source for
// modern Visual Studio. These derive from the stock MSBuild ToolTask base
// (Microsoft.Build.Utilities), which is unchanged across VS versions, so they
// are portable as-is; they are rebuilt here only to live alongside the CL/Link
// tasks in one RXDK-360 assembly. See CL.cs for the overall rationale.

using System;
using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Text;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Rxdk.Xbox360.Build.Tasks
{
    /// <summary>Base for XDK command-line tools found under &lt;XDK&gt;\bin\win32.</summary>
    public abstract class XboxToolTask : ToolTask
    {
        [Required]
        public ITaskItem XDKInstallDir { get; set; }

        /// <summary>Directory that contains the XDK host tools (cl, xbcp, imagexex).
        /// Product-root <c>bin\win32</c>; falls back to <see cref="XDKInstallDir"/>\bin\win32.</summary>
        public ITaskItem XDKBinDir { get; set; }

        protected override string GenerateFullPathToTool()
        {
            string bin = (XDKBinDir != null && !string.IsNullOrEmpty(XDKBinDir.ItemSpec))
                ? XDKBinDir.ItemSpec
                : Path.Combine(XDKInstallDir.ItemSpec, "bin\\win32");
            return Path.Combine(bin, ToolName);
        }
    }

    /// <summary>Common state for the console-deploy tools.</summary>
    public abstract class Deploy : XboxToolTask
    {
        public string TargetConsole { get; set; }

        public bool SuppressStartupBanner { get; set; }
    }

    /// <summary>
    /// Copies the built title (and any additional deployment files) to a devkit
    /// console's hard drive via the XDK's xbecopy.exe.
    /// </summary>
    public class DeployToHardDrive : Deploy
    {
        protected override string ToolName
        {
            get { return "xbecopy.exe"; }
        }

        [Required]
        public string[] DeploymentFiles { get; set; }

        [Required]
        public string DeploymentRoot { get; set; }

        public bool Progress { get; set; }

        public bool ForceCopy { get; set; }

        [Required]
        public ITaskItem ProjectDir { get; set; }

        public string DefaultConsole { get; set; }

        protected override string GenerateResponseFileCommands()
        {
            StringBuilder stringBuilder = new StringBuilder();
            if (SuppressStartupBanner)
            {
                stringBuilder.Append("/nologo\n");
            }
            if (Progress)
            {
                stringBuilder.Append("/progress\n");
            }
            if (ForceCopy)
            {
                stringBuilder.Append("/forcecopy\n");
            }
            Hashtable hashtable = new Hashtable();
            char[] trimChars = new char[1] { '\\' };
            if (!string.IsNullOrEmpty(TargetConsole))
            {
                stringBuilder.Append("\"/xbox:");
                stringBuilder.Append(TargetConsole);
                stringBuilder.Append("\"\n");
            }
            string[] deploymentFiles = DeploymentFiles;
            foreach (string text in deploymentFiles)
            {
                if (!text.Contains("="))
                {
                    string path = Path.Combine(ProjectDir.ItemSpec, text);
                    string fullPath = Path.GetFullPath(path);
                    if (!hashtable.Contains(fullPath.ToLower()))
                    {
                        stringBuilder.Append("\"");
                        stringBuilder.Append(fullPath);
                        stringBuilder.Append("\" \"");
                        stringBuilder.Append(DeploymentRoot);
                        if (!DeploymentRoot.EndsWith("\\"))
                        {
                            stringBuilder.Append("\\");
                        }
                        string text2 = ((!fullPath.StartsWith(ProjectDir.ItemSpec, StringComparison.OrdinalIgnoreCase)) ? Path.GetFileName(fullPath) : fullPath.Remove(0, ProjectDir.ItemSpec.Length));
                        text2 = text2.TrimStart(trimChars);
                        stringBuilder.Append(text2);
                        stringBuilder.Append("\"\n");
                        hashtable.Add(fullPath.ToLower(), null);
                    }
                    continue;
                }
                char[] separator = new char[1] { '=' };
                string[] array = text.Split(separator);
                string text3 = array[0];
                string text4 = array[1];
                text3 = text3.Trim();
                text4 = Path.Combine(ProjectDir.ItemSpec, text4.Trim());
                string fullPath2 = Path.GetFullPath(text4);
                if (hashtable.Contains(fullPath2.ToLower()))
                {
                    continue;
                }
                FileAttributes attributes = File.GetAttributes(fullPath2);
                if ((attributes & FileAttributes.Directory) == 0)
                {
                    if (!text3.EndsWith("\\"))
                    {
                        text3 += "\\";
                    }
                    text3 += Path.GetFileName(fullPath2);
                }
                stringBuilder.Append("\"");
                stringBuilder.Append(fullPath2);
                stringBuilder.Append("\" \"");
                if (text3.Contains(":"))
                {
                    stringBuilder.Append(text3);
                }
                else
                {
                    stringBuilder.Append(DeploymentRoot);
                    if (!DeploymentRoot.EndsWith("\\"))
                    {
                        stringBuilder.Append("\\");
                    }
                    text3 = text3.TrimStart(trimChars);
                    stringBuilder.Append(text3);
                }
                stringBuilder.Append("\"\n");
                hashtable.Add(fullPath2.ToLower(), null);
            }
            return stringBuilder.ToString();
        }
    }

    /// <summary>
    /// Official VS 2010 Xbox 360 debugger (X360EmulationManagerV100) on Emulate DVD:
    /// write a Game Disc layout (.xgd) from Deployment Files, then xbEmulate /Media
    /// /Emulate start. Without the DVD EMU USB sidecar that fails; the message is
    /// the stock VS dialog text (observed from that DLL).
    /// </summary>
    public class EmulateDvd : Task
    {
        public string[] DeploymentFiles { get; set; }
        public string LayoutFile { get; set; }
        public string DvdEmulationType { get; set; }
        public string TargetConsole { get; set; }
        public string DefaultConsole { get; set; }
        public string OutputXgd { get; set; }
        public string StagingDir { get; set; }
        public ITaskItem ProjectDir { get; set; }
        public ITaskItem ImagePath { get; set; }
        [Required]
        public ITaskItem XDKInstallDir { get; set; }
        public ITaskItem XDKBinDir { get; set; }

        public override bool Execute()
        {
            string console = FirstNonEmpty(TargetConsole, DefaultConsole);
            string bin = BinDir();
            string xbEmulate = Path.Combine(bin, "xbEmulate.exe");
            if (!File.Exists(xbEmulate))
            {
                Log.LogError("xbEmulate.exe not found at {0}. Install RXDK-360 / the Xbox 360 XDK bin\\win32 tools.", xbEmulate);
                return false;
            }

            string xgd;
            try
            {
                xgd = ResolveLayout(console);
            }
            catch (Exception ex)
            {
                Log.LogError("Could not build the layout for this project: {0}", ex.Message);
                return false;
            }

            Log.LogMessage(MessageImportance.High, "Console Deployment/Generating Layout for Optical Disc Emulation...");
            Log.LogMessage(MessageImportance.High, "  {0}", xgd);

            string timing = TimingMode(DvdEmulationType);
            if (!DvdEmulation.TryStart(xbEmulate, xgd, timing, console, out string error, out string log))
            {
                if (!string.IsNullOrEmpty(log))
                    Log.LogMessage(MessageImportance.Low, log);
                Log.LogError(error);
                return false;
            }
            return true;
        }

        private string ResolveLayout(string console)
        {
            string layout = LayoutFile ?? "";
            if (layout.StartsWith("<", StringComparison.Ordinal))
                layout = "";
            if (layout.IndexOf("Use Deployment Files", StringComparison.OrdinalIgnoreCase) >= 0)
                layout = "";
            if (!string.IsNullOrEmpty(layout) && File.Exists(layout))
                return Path.GetFullPath(layout);

            string projectDir = ProjectDir != null ? ProjectDir.ItemSpec : Environment.CurrentDirectory;
            string image = ImagePath != null ? ImagePath.ItemSpec : "";
            string stage = StagingDir;
            if (string.IsNullOrEmpty(stage))
                stage = Path.Combine(projectDir, "obj", "dvdlayout");
            string xgd = OutputXgd;
            if (string.IsNullOrEmpty(xgd))
                xgd = Path.Combine(projectDir, Path.GetFileNameWithoutExtension(image) + ".xgd");
            Directory.CreateDirectory(Path.GetDirectoryName(xgd) ?? projectDir);
            return DvdEmulation.WriteGeneratedLayout(stage, xgd, DeploymentFiles, projectDir, image);
        }

        private string BinDir()
        {
            if (XDKBinDir != null && !string.IsNullOrEmpty(XDKBinDir.ItemSpec))
                return XDKBinDir.ItemSpec;
            return Path.Combine(XDKInstallDir.ItemSpec, "bin\\win32");
        }

        internal static string TimingMode(string type)
        {
            if (string.Equals(type, "TypicalSeekTimes", StringComparison.OrdinalIgnoreCase))
                return "typical";
            if (string.Equals(type, "AccurateSeekTimes", StringComparison.OrdinalIgnoreCase))
                return "accurate";
            return "none";
        }

        private static string FirstNonEmpty(params string[] values)
        {
            foreach (var v in values)
                if (!string.IsNullOrWhiteSpace(v))
                    return v.Trim();
            return "";
        }
    }

    /// <summary>XGD write + xbEmulate start, shared shape with the VS F5 launcher.</summary>
    internal static class DvdEmulation
    {
        public static string SessionFailedMessage(string console)
        {
            string who = string.IsNullOrWhiteSpace(console) ? "the default console" : console.Trim();
            return "Failed to create emulation session for '" + who + "'.\r\n" +
                   "Ensure that this computer is connected via USB to the port labeled 'DVD EMU' on the console.";
        }

        public static string WriteGeneratedLayout(string stagingDir, string xgdPath, string[] deploymentFiles, string projectDir, string imagePath)
        {
            if (Directory.Exists(stagingDir))
                Directory.Delete(stagingDir, true);
            Directory.CreateDirectory(stagingDir);

            if (!string.IsNullOrEmpty(imagePath) && File.Exists(imagePath))
            {
                string name = Path.GetFileName(imagePath);
                File.Copy(imagePath, Path.Combine(stagingDir, "default.xex"), true);
                if (!name.Equals("default.xex", StringComparison.OrdinalIgnoreCase))
                    File.Copy(imagePath, Path.Combine(stagingDir, name), true);
            }

            if (deploymentFiles != null)
            {
                foreach (string spec in deploymentFiles)
                    StageDeploymentSpec(spec, projectDir, stagingDir, imagePath);
            }

            if (Directory.GetFileSystemEntries(stagingDir).Length == 0)
                throw new InvalidOperationException("No files to put on the emulated disc (ImagePath / Deployment Files).");

            string source = stagingDir.TrimEnd('\\') + "\\";
            var sb = new StringBuilder();
            sb.AppendLine("<LAYOUT MAJORVERSION=\"2\" MINORVERSION=\"2\">");
            sb.AppendLine("    <AVATARASSETPACK INCLUDE=\"NO\"/>");
            sb.AppendLine("    <DISC NAME=\"\" LayoutType=\"XGD2\">");
            sb.Append("        <ADD NAME=\"\" SOURCE=\"");
            sb.Append(EscapeXml(source));
            sb.AppendLine("\" DEST=\"\\\" FILESPEC=\"*.*\" RECURSE=\"YES\" LAYER=\"ANY\" ALIGN=\"1\"/>");
            sb.AppendLine("    </DISC>");
            sb.AppendLine("</LAYOUT>");
            File.WriteAllText(xgdPath, sb.ToString(), new UTF8Encoding(false));
            return xgdPath;
        }

        public static bool TryStart(string xbEmulate, string xgd, string timingMode, string console, out string error, out string log)
        {
            error = "";
            var psi = new ProcessStartInfo
            {
                FileName = xbEmulate,
                Arguments = "/nologo /Media \"" + xgd + "\" /TimingMode " + timingMode + " /Emulate start",
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
            };
            if (!string.IsNullOrEmpty(console) && !LooksLikeIp(console))
                psi.Arguments = "/nologo /Console \"" + console + "\" /Media \"" + xgd +
                    "\" /TimingMode " + timingMode + " /Emulate start";

            var output = new StringBuilder();
            using (var p = Process.Start(psi))
            {
                if (p == null)
                {
                    error = SessionFailedMessage(console);
                    log = "";
                    return false;
                }
                output.AppendLine(p.StandardOutput.ReadToEnd());
                output.AppendLine(p.StandardError.ReadToEnd());
                p.WaitForExit();
                log = output.ToString();
                if (p.ExitCode == 0 && log.IndexOf("ERROR:", StringComparison.OrdinalIgnoreCase) < 0)
                    return true;
            }
            error = SessionFailedMessage(console);
            return false;
        }

        private static void StageDeploymentSpec(string spec, string projectDir, string stagingDir, string imagePath)
        {
            if (string.IsNullOrWhiteSpace(spec))
                return;
            string src;
            if (spec.Contains("="))
            {
                string[] parts = spec.Split(new[] { '=' }, 2);
                src = parts[1].Trim();
            }
            else src = spec.Trim();
            src = Path.GetFullPath(Path.Combine(projectDir ?? "", src));
            if (!string.IsNullOrEmpty(imagePath) &&
                string.Equals(Path.GetFullPath(imagePath), src, StringComparison.OrdinalIgnoreCase))
                return;
            if (!File.Exists(src) && !Directory.Exists(src))
                return;
            if (Directory.Exists(src))
            {
                foreach (var file in Directory.GetFiles(src, "*", SearchOption.AllDirectories))
                {
                    string rel = file.Substring(src.TrimEnd('\\').Length).TrimStart('\\');
                    string dest = Path.Combine(stagingDir, rel);
                    Directory.CreateDirectory(Path.GetDirectoryName(dest) ?? stagingDir);
                    File.Copy(file, dest, true);
                }
                return;
            }
            File.Copy(src, Path.Combine(stagingDir, Path.GetFileName(src)), true);
        }

        private static bool LooksLikeIp(string s)
        {
            int dots = 0;
            foreach (char c in s)
                if (c == '.') dots++;
                else if (c != ':' && !char.IsDigit(c)) return false;
            return dots == 3;
        }

        private static string EscapeXml(string s)
        {
            if (string.IsNullOrEmpty(s)) return "";
            return s.Replace("&", "&amp;").Replace("\"", "&quot;").Replace("<", "&lt;").Replace(">", "&gt;");
        }
    }

    /// <summary>
    /// Wraps the XDK's imagexex.exe to turn a linked PE into a bootable .xex,
    /// applying the title id, base address, heap/workspace sizes and the full
    /// set of title privileges (optical-disc mapping, PAL50, multi-disc,
    /// Kinect, ...).
    /// </summary>
    public class ImageXex : XboxToolTask
    {
        protected override string ToolName
        {
            get { return "imagexex.exe"; }
        }

        [Required]
        public ITaskItem InputFile { get; set; }

        [Required]
        public ITaskItem OutputFile { get; set; }

        public string TitleID { get; set; }

        public string LanKey { get; set; }

        public bool SuppressStartupBanner { get; set; }

        public string BaseAddress { get; set; }

        public string HeapSize { get; set; }

        public string WorkspaceSize { get; set; }

        public string[] AdditionalSections { get; set; }

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

        public ITaskItem ConfigurationFile { get; set; }

        protected override string GenerateCommandLineCommands()
        {
            StringBuilder stringBuilder = new StringBuilder();
            stringBuilder.Append(string.Format("/out:\"{0}\" ", OutputFile.ItemSpec));
            if (!string.IsNullOrEmpty(TitleID))
            {
                stringBuilder.Append(string.Format("/titleid:\"{0}\" ", TitleID));
            }
            if (!string.IsNullOrEmpty(LanKey))
            {
                stringBuilder.Append(string.Format("/lankey:\"{0}\" ", LanKey));
            }
            if (SuppressStartupBanner)
            {
                stringBuilder.Append("/nologo ");
            }
            if (!string.IsNullOrEmpty(BaseAddress))
            {
                stringBuilder.Append(string.Format("/baseaddr:\"{0}\" ", BaseAddress));
            }
            if (!string.IsNullOrEmpty(HeapSize))
            {
                stringBuilder.Append(string.Format("/xapiheap:\"{0}\" ", HeapSize));
            }
            if (!string.IsNullOrEmpty(WorkspaceSize))
            {
                stringBuilder.Append(string.Format("/workspace:\"{0}\" ", WorkspaceSize));
            }
            if (AdditionalSections != null)
            {
                string[] additionalSections = AdditionalSections;
                foreach (string text in additionalSections)
                {
                    if (!string.IsNullOrEmpty(text))
                    {
                        stringBuilder.Append(string.Format("/section:\"{0}\" ", text));
                    }
                }
            }
            if (ExportByName)
            {
                stringBuilder.Append("/exportnames ");
            }
            if (OpticalDiscDriveMapping)
            {
                stringBuilder.Append("/privilege:2 ");
            }
            if (Pal50Incompatible)
            {
                stringBuilder.Append("/privilege:10 ");
            }
            if (MultiDiscTitle)
            {
                stringBuilder.Append("/privilege:15 /privilege:16 ");
            }
            if (PreferBigButtonInput)
            {
                stringBuilder.Append("/privilege:25 ");
            }
            if (CrossPlatformSystemLink)
            {
                stringBuilder.Append("/privilege:14 ");
            }
            if (AllowAvatarGetMetadata)
            {
                stringBuilder.Append("/privilege:29 ");
            }
            if (AllowControllerSwapping)
            {
                stringBuilder.Append("/privilege:30 ");
            }
            if (RequireFullExperience)
            {
                stringBuilder.Append("/privilege:34 ");
            }
            if (GameVoiceRequiredUI)
            {
                stringBuilder.Append("/privilege:35 ");
            }
            if (ConfigurationFile != null && !string.IsNullOrEmpty(ConfigurationFile.ItemSpec))
            {
                stringBuilder.Append(string.Format("/config:\"{0}\" ", ConfigurationFile.ItemSpec));
            }
            if (KinectElevationControl)
            {
                stringBuilder.Append("/privilege:37 ");
            }
            if (!string.IsNullOrEmpty(KinectSupportLevel))
            {
                if (string.Equals(KinectSupportLevel, "RequiresTracking", StringComparison.OrdinalIgnoreCase))
                {
                    stringBuilder.Append("/privilege:38 ");
                }
                else if (string.Equals(KinectSupportLevel, "SupportsTracking", StringComparison.OrdinalIgnoreCase))
                {
                    stringBuilder.Append("/privilege:39 ");
                }
            }
            stringBuilder.Append(string.Format("\"{0}\"", InputFile.ItemSpec));
            return stringBuilder.ToString();
        }
    }
}
