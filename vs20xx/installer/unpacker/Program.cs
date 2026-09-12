// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// RXDK-360 XDK unpacker. See RxdkXdkUnpacker.csproj for the format notes: an
// Xbox 360 XDK setup EXE is a PE stub followed by a chain of concatenated
// standard MS cabinets. This walks the chain and extracts each cabinet in
// process via the Windows FDI API (cabinet.dll).

using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

namespace Rxdk.Xdk.Unpacker
{
    internal static class Program
    {
        private const string UndoLog = "rxdk360-uninstall.log";

        private static int Main(string[] args)
        {
            try
            {
                // verbs: unpack <setup> <outDir> | install <setup> <installDir> | uninstall <installDir>
                if (args.Length >= 1 && args[0].Equals("uninstall", StringComparison.OrdinalIgnoreCase))
                {
                    if (args.Length < 2) return Usage();
                    ManifestInstaller.Uninstall(Path.Combine(args[1], UndoLog));
                    Console.WriteLine("uninstalled from " + args[1]);
                    return 0;
                }

                bool dryRun = Array.FindIndex(args, s => s.Equals("--dry-run", StringComparison.OrdinalIgnoreCase)) >= 0;
                var pos = new List<string>();
                foreach (var s in args) if (!s.StartsWith("--")) pos.Add(s);
                args = pos.ToArray();

                bool install = args.Length >= 1 && args[0].Equals("install", StringComparison.OrdinalIgnoreCase);
                int a = install ? 1 : 0;
                if (args.Length - a < 2) return Usage();
                string setupExe = args[a];
                string dest = args[a + 1];
                bool preExtracted = install && Directory.Exists(setupExe) && File.Exists(Path.Combine(setupExe, "manifest.csv"));
                if (!preExtracted && !File.Exists(setupExe)) { Console.Error.WriteLine("not found: " + setupExe); return 2; }

                if (!install)
                {
                    // plain unpack: extract the raw XDK\... tree to <outDir>
                    Directory.CreateDirectory(dest);
                    ExtractAll(setupExe, dest);
                    return 0;
                }

                // install: extract to a temp staging area (unless already extracted),
                // then replay manifest.csv relocated for RXDK-360.
                string staging = preExtracted ? setupExe
                    : Path.Combine(Path.GetTempPath(), "rxdk_stage_" + Guid.NewGuid().ToString("N"));
                try
                {
                    if (!preExtracted) { Directory.CreateDirectory(staging); ExtractAll(setupExe, staging); }
                    Directory.CreateDirectory(dest);
                    Console.WriteLine("applying manifest -> {0}{1}", dest, dryRun ? "  (DRY RUN)" : "");
                    new ManifestInstaller(staging, dest) { DryRun = dryRun }.Run(Path.Combine(dest, UndoLog));
                    Console.WriteLine("done.");
                }
                finally { if (!preExtracted) { try { Directory.Delete(staging, true); } catch { } } }
                return 0;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("error: " + ex.Message);
                return 1;
            }
        }

        private static int Usage()
        {
            Console.Error.WriteLine("usage:");
            Console.Error.WriteLine("  RxdkXdkUnpacker <XDKSetup.exe> <outDir>            (extract the XDK\\ tree)");
            Console.Error.WriteLine("  RxdkXdkUnpacker install <XDKSetup.exe> <installDir> (manifest-driven install)");
            Console.Error.WriteLine("  RxdkXdkUnpacker uninstall <installDir>              (reverse an install)");
            return 2;
        }

        // Extract every cabinet in the setup EXE to destDir. Returns file count.
        private static int ExtractAll(string setupExe, string destDir)
        {
            var cabs = FindCabinets(setupExe);
            Console.WriteLine("found {0} cabinet(s) in {1}", cabs.Count, Path.GetFileName(setupExe));
            if (cabs.Count == 0)
                throw new InvalidDataException("no MSCF cabinet found - is this an Xbox 360 XDK setup EXE?");

            int total = 0;
            string tmp = Path.Combine(Path.GetTempPath(), "rxdk_cab_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(tmp);
            try
            {
                using (var fs = File.OpenRead(setupExe))
                {
                    int i = 0;
                    foreach (var c in cabs)
                    {
                        string part = Path.Combine(tmp, "part.cab");
                        CarveTo(fs, c.Offset, c.Size, part);
                        using (var fdi = new FdiExtractor())
                            total += fdi.Extract(part, destDir);
                        Console.WriteLine("  cab {0}/{1}: {2} files ({3:n0} bytes)", ++i, cabs.Count, c.Files, c.Size);
                        File.Delete(part);
                    }
                }
            }
            finally { try { Directory.Delete(tmp, true); } catch { } }
            Console.WriteLine("extracted {0} files", total);
            return total;
        }

        private struct CabRef { public long Offset; public long Size; public int Files; }

        // Walk the concatenated cabinet chain starting at the PE overlay.
        private static List<CabRef> FindCabinets(string path)
        {
            var list = new List<CabRef>();
            using (var fs = File.OpenRead(path))
            using (var br = new BinaryReader(fs))
            {
                long overlay = OverlayStart(fs, br);
                long off = overlay;
                long len = fs.Length;
                var hdr = new byte[36];
                while (off + 36 <= len)
                {
                    fs.Position = off;
                    if (fs.Read(hdr, 0, 36) != 36) break;
                    if (!(hdr[0] == 'M' && hdr[1] == 'S' && hdr[2] == 'C' && hdr[3] == 'F')) break;
                    long cbCab = BitConverter.ToUInt32(hdr, 8);
                    ushort cFiles = BitConverter.ToUInt16(hdr, 28);
                    if (cbCab < 36 || off + cbCab > len) break;
                    list.Add(new CabRef { Offset = off, Size = cbCab, Files = cFiles });
                    off += cbCab;
                }
            }
            return list;
        }

        // End of the last PE section = start of the appended cabinet payload.
        private static long OverlayStart(FileStream fs, BinaryReader br)
        {
            fs.Position = 0x3c;
            uint pe = br.ReadUInt32();
            fs.Position = pe + 6;
            ushort nsec = br.ReadUInt16();
            fs.Position = pe + 20;
            ushort optSize = br.ReadUInt16();
            long secTab = pe + 24 + optSize;
            long end = 0;
            for (int i = 0; i < nsec; i++)
            {
                fs.Position = secTab + i * 40 + 16;
                uint rawSize = br.ReadUInt32();
                uint rawPtr = br.ReadUInt32();
                end = Math.Max(end, (long)rawPtr + rawSize);
            }
            return end;
        }

        private static void CarveTo(FileStream src, long offset, long size, string dest)
        {
            src.Position = offset;
            using (var dst = File.Create(dest))
            {
                var buf = new byte[1 << 20];
                long remaining = size;
                while (remaining > 0)
                {
                    int want = (int)Math.Min(buf.Length, remaining);
                    int got = src.Read(buf, 0, want);
                    if (got <= 0) throw new IOException("unexpected EOF carving cabinet");
                    dst.Write(buf, 0, got);
                    remaining -= got;
                }
            }
        }
    }
}
