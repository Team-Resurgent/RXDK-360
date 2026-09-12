// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// Minimal in-process cabinet extractor over the Windows FDI API (cabinet.dll).
// Extracts one standard MS cabinet, preserving the paths stored in it
// (e.g. "XDK\bin\win32\xbpg.exe") under a destination directory.

using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

namespace Rxdk.Xdk.Unpacker
{
    internal sealed class FdiExtractor : IDisposable
    {
        // ---- FDI P/Invoke (cabinet.dll) ---------------------------------------
        [StructLayout(LayoutKind.Sequential)]
        private struct ERF { public int erfOper; public int erfType; public int fError; }

        [StructLayout(LayoutKind.Sequential)]
        private struct FDINOTIFICATION
        {
            public int cb;
            public IntPtr psz1;
            public IntPtr psz2;
            public IntPtr psz3;
            public IntPtr pv;
            public IntPtr hf;
            public short date;
            public short time;
            public short attribs;
            public short setID;
            public short iCabinet;
            public short iFolder;
            public int fdie;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr PfnAlloc(int cb);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void PfnFree(IntPtr pv);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr PfnOpen(string path, int oflag, int pmode);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int PfnRead(IntPtr hf, IntPtr pv, int cb);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int PfnWrite(IntPtr hf, IntPtr pv, int cb);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int PfnClose(IntPtr hf);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int PfnSeek(IntPtr hf, int dist, int seektype);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr PfnNotify(int fdint, ref FDINOTIFICATION pfdin);

        [DllImport("cabinet.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern IntPtr FDICreate(PfnAlloc a, PfnFree f, PfnOpen o, PfnRead r,
            PfnWrite w, PfnClose c, PfnSeek s, int cpuType, ref ERF erf);

        [DllImport("cabinet.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int FDICopy(IntPtr hfdi, string cabinet, string cabPath, int flags,
            PfnNotify notify, IntPtr decrypt, IntPtr userData);

        [DllImport("cabinet.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int FDIDestroy(IntPtr hfdi);

        private const int fdintCOPY_FILE = 2;
        private const int fdintCLOSE_FILE_INFO = 3;

        // ---- handle table (FDI hands us opaque IntPtr handles) ----------------
        private readonly Dictionary<int, Stream> _handles = new Dictionary<int, Stream>();
        private int _nextHandle = 1;
        private string _destDir;
        private int _extracted;

        // keep delegates alive for the duration of the call
        private readonly PfnAlloc _a; private readonly PfnFree _f; private readonly PfnOpen _o;
        private readonly PfnRead _r; private readonly PfnWrite _w; private readonly PfnClose _c;
        private readonly PfnSeek _s; private readonly PfnNotify _n;

        public FdiExtractor()
        {
            _a = cb => Marshal.AllocHGlobal(cb);
            _f = Marshal.FreeHGlobal;
            _o = OnOpen; _r = OnRead; _w = OnWrite; _c = OnClose; _s = OnSeek; _n = OnNotify;
        }

        public int Extract(string cabFile, string destDir)
        {
            _destDir = destDir;
            _extracted = 0;
            var erf = new ERF();
            IntPtr hfdi = FDICreate(_a, _f, _o, _r, _w, _c, _s, 0 /*cpuUNKNOWN*/, ref erf);
            if (hfdi == IntPtr.Zero)
                throw new IOException("FDICreate failed (erfOper=" + erf.erfOper + ")");
            try
            {
                string dir = Path.GetDirectoryName(cabFile);
                string name = Path.GetFileName(cabFile);
                if (FDICopy(hfdi, name, dir + Path.DirectorySeparatorChar, 0, _n, IntPtr.Zero, IntPtr.Zero) == 0)
                    throw new IOException("FDICopy failed (erfOper=" + erf.erfOper + ", erfType=" + erf.erfType + ")");
            }
            finally { FDIDestroy(hfdi); }
            return _extracted;
        }

        private IntPtr Register(Stream s)
        {
            int id = _nextHandle++;
            _handles[id] = s;
            return (IntPtr)id;
        }

        private IntPtr OnOpen(string path, int oflag, int pmode)
        {
            // FDI opens the cabinet file for reading.
            try { return Register(File.Open(path, FileMode.Open, FileAccess.Read, FileShare.Read)); }
            catch { return (IntPtr)(-1); }
        }

        private int OnRead(IntPtr hf, IntPtr pv, int cb)
        {
            if (!_handles.TryGetValue((int)hf, out var s)) return -1;
            var buf = new byte[cb];
            int got = s.Read(buf, 0, cb);
            if (got > 0) Marshal.Copy(buf, 0, pv, got);
            return got;
        }

        private int OnWrite(IntPtr hf, IntPtr pv, int cb)
        {
            if (!_handles.TryGetValue((int)hf, out var s)) return -1;
            var buf = new byte[cb];
            Marshal.Copy(pv, buf, 0, cb);
            s.Write(buf, 0, cb);
            return cb;
        }

        private int OnClose(IntPtr hf)
        {
            if (_handles.TryGetValue((int)hf, out var s)) { s.Dispose(); _handles.Remove((int)hf); }
            return 0;
        }

        private int OnSeek(IntPtr hf, int dist, int seektype)
        {
            if (!_handles.TryGetValue((int)hf, out var s)) return -1;
            return (int)s.Seek(dist, (SeekOrigin)seektype);
        }

        private IntPtr OnNotify(int fdint, ref FDINOTIFICATION p)
        {
            switch (fdint)
            {
                case fdintCOPY_FILE:
                {
                    string rel = Marshal.PtrToStringAnsi(p.psz1) ?? "";
                    string outPath = Path.Combine(_destDir, rel.Replace('/', Path.DirectorySeparatorChar));
                    Directory.CreateDirectory(Path.GetDirectoryName(outPath));
                    return Register(File.Create(outPath));
                }
                case fdintCLOSE_FILE_INFO:
                {
                    OnClose(p.hf);
                    _extracted++;
                    return (IntPtr)1; // TRUE
                }
                default:
                    return IntPtr.Zero;
            }
        }

        public void Dispose()
        {
            foreach (var s in _handles.Values) { try { s.Dispose(); } catch { } }
            _handles.Clear();
        }
    }
}
