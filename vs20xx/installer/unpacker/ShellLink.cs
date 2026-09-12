// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// Minimal IShellLink wrapper to create .lnk shortcuts (for manifest 'shortcut'
// rows) without a dependency on Windows Script Host.

using System;
using System.Runtime.InteropServices;

namespace Rxdk.Xdk.Unpacker
{
    internal static class ShellLink
    {
        public static void Create(string linkPath, string target, string args,
                                   string workingDir, string description, string iconPath, int iconIndex)
        {
            var link = (IShellLinkW)new CShellLink();
            link.SetPath(target);
            if (!string.IsNullOrEmpty(args)) link.SetArguments(args);
            if (!string.IsNullOrEmpty(workingDir)) link.SetWorkingDirectory(workingDir);
            if (!string.IsNullOrEmpty(description)) link.SetDescription(description);
            if (!string.IsNullOrEmpty(iconPath)) link.SetIconLocation(iconPath, iconIndex);
            ((IPersistFile)link).Save(linkPath, true);
            Marshal.ReleaseComObject(link);
        }

        [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
        private class CShellLink { }

        [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown),
         Guid("000214F9-0000-0000-C000-000000000046")]
        private interface IShellLinkW
        {
            void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder f, int cch, IntPtr fd, int flags);
            void GetIDList(out IntPtr ppidl);
            void SetIDList(IntPtr pidl);
            void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder n, int cch);
            void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string n);
            void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder d, int cch);
            void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string d);
            void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder a, int cch);
            void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string a);
            void GetHotkey(out short w);
            void SetHotkey(short w);
            void GetShowCmd(out int cmd);
            void SetShowCmd(int cmd);
            void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder i, int cch, out int idx);
            void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string i, int idx);
            void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string pathRel, int reserved);
            void Resolve(IntPtr hwnd, int flags);
            void SetPath([MarshalAs(UnmanagedType.LPWStr)] string path);
        }

        [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown),
         Guid("0000010b-0000-0000-C000-000000000046")]
        private interface IPersistFile
        {
            void GetClassID(out Guid id);
            [PreserveSig] int IsDirty();
            void Load([MarshalAs(UnmanagedType.LPWStr)] string fileName, int mode);
            void Save([MarshalAs(UnmanagedType.LPWStr)] string fileName, [MarshalAs(UnmanagedType.Bool)] bool remember);
            void SaveCompleted([MarshalAs(UnmanagedType.LPWStr)] string fileName);
            void GetCurFile([MarshalAs(UnmanagedType.LPWStr)] out string fileName);
        }
    }
}
