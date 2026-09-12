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

        protected override string GenerateFullPathToTool()
        {
            return Path.Combine(XDKInstallDir.ItemSpec, Path.Combine("bin\\win32", ToolName));
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
