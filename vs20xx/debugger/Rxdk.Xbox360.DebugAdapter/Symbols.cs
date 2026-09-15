// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.IO;
using Rxdk.Xbox360.Pdb;
using Rxdk.Xbox360.Dwarf;
using Rxdk.Xbox360.Xbdm;

namespace Rxdk.Xbox360.DebugAdapter
{
    /// <summary>Where a local lives at a stop: either a guest memory address or a register.</summary>
    public readonly struct VarSlot
    {
        public readonly string Name, TypeName;
        public readonly int Size;
        public readonly uint? Address;   // memory location
        public readonly int? Register;   // or GPR index
        public readonly ulong TypeOffset;
        public VarSlot(string name, string type, int size, uint? addr, int? reg, ulong typeOffset = 0)
        { Name = name; TypeName = type; Size = size; Address = addr; Register = reg; TypeOffset = typeOffset; }
    }

    /// <summary>A source position for a kit PC or a planted breakpoint.</summary>
    public readonly struct SourceLocation
    {
        public readonly string File, Function;
        public readonly int Line;
        public SourceLocation(string file, int line, string function)
        { File = file; Line = line; Function = function ?? ""; }
    }

    /// <summary>
    /// The symbol side of the adapter: DWARF (.elf, clang) or PDB/XDB (legacy 2010-01).
    /// Resolves file:line → address (RVA or exe VMA; the session relocates onto the live XEX)
    /// and kit PC → source. PDB locals/hover go through <see cref="ManagedValues"/>.
    /// </summary>
    public sealed class Symbols
    {
        public DwarfInfo? Info { get; }
        public string Path { get; }
        public string Kind { get; }
        public PdbImage? Pdb => _pdb;
        private readonly PdbImage? _pdb;

        public Symbols(DwarfInfo info, string path) { Info = info; Path = path; Kind = "elf"; }
        private Symbols(PdbImage pdb, string path) { _pdb = pdb; Path = path; Kind = System.IO.Path.GetExtension(path).TrimStart('.').ToLowerInvariant(); }

        public static Symbols FromElf(string elfPath) => new(DwarfReader.Read(elfPath), elfPath);
        public static Symbols FromPdb(string pdbPath) => new(PdbImage.OpenFile(pdbPath), pdbPath);

        /// <summary>Prefer DWARF beside the XEX, then .pdb (C13 lines), then .xdb.</summary>
        public static Symbols? Open(string program, string? symbols = null)
        {
            var candidates = new List<string>();
            if (!string.IsNullOrEmpty(symbols)) candidates.Add(symbols);
            if (!string.IsNullOrEmpty(program))
            {
                string dir = System.IO.Path.GetDirectoryName(program) ?? "";
                string name = System.IO.Path.GetFileNameWithoutExtension(program);
                candidates.Add(System.IO.Path.ChangeExtension(program, ".elf"));
                candidates.Add(System.IO.Path.Combine(dir, name + ".pdb"));
                candidates.Add(System.IO.Path.ChangeExtension(program, ".pdb"));
                candidates.Add(System.IO.Path.Combine(dir, name + ".xdb"));
                candidates.Add(System.IO.Path.ChangeExtension(program, ".xdb"));
            }

            Symbols? emptyPdb = null;
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var p in candidates)
            {
                if (string.IsNullOrEmpty(p) || !seen.Add(p) || !File.Exists(p)) continue;
                string ext = System.IO.Path.GetExtension(p);
                try
                {
                    if (ext.Equals(".elf", StringComparison.OrdinalIgnoreCase))
                        return FromElf(p);
                    if (ext.Equals(".pdb", StringComparison.OrdinalIgnoreCase) ||
                        ext.Equals(".xdb", StringComparison.OrdinalIgnoreCase))
                    {
                        var s = FromPdb(p);
                        if (s.HasLines) return s;
                        emptyPdb ??= s;
                    }
                }
                catch { /* try the next candidate */ }
            }
            return emptyPdb;
        }

        public bool HasLines =>
            _pdb != null
                ? _pdb.Lines.Entries.Count > 0
                : Info != null && HasDwarfLines(Info);

        /// <summary>Lowest address for a source file:line. DWARF returns a VMA; PDB returns an RVA.
        /// <see cref="TitleAddress.ExeToXex"/> accepts both.</summary>
        public (ulong address, int line)? ResolveBreakpoint(string file, int line)
        {
            if (_pdb != null)
            {
                if (_pdb.TryResolveLine(file, (uint)line, out uint rva))
                    return (rva, line);
                return null;
            }
            if (Info == null) return null;
            var exact = Info.AddressFor(file, line);
            if (exact != null) return (exact.Value, line);
            int bestLine = int.MaxValue; ulong bestAddr = 0; bool found = false;
            foreach (var u in Info.Units)
                foreach (var r in u.Lines)
                {
                    if (r.EndSequence || r.Line < line) continue;
                    if (!Match(file, r.File)) continue;
                    if (r.Line < bestLine) { bestLine = r.Line; bestAddr = r.Address; found = true; }
                }
            return found ? (bestAddr, bestLine) : null;
        }

        public SourceLocation? LineAtKit(uint kitPc, uint xexBase)
        {
            if (_pdb != null)
            {
                uint rva = TitleAddress.XexToRva(kitPc, xexBase != 0 ? xexBase : TitleAddress.DefaultBase);
                if (_pdb.TryFindLine(rva, out var file, out var line))
                {
                    _pdb.TryFindFunctionName(rva, out var func);
                    return new SourceLocation(file, (int)line, func);
                }
                return null;
            }
            if (Info == null) return null;
            ulong exe = TitleAddress.XexToExe(kitPc, xexBase != 0 ? xexBase : TitleAddress.DefaultBase);
            var ln = Info.LineAt(exe);
            if (ln == null) return null;
            var fn = Info.FunctionAt(exe);
            return new SourceLocation(ln.File, ln.Line, fn?.Name ?? "");
        }

        public DwarfFunction? FunctionAt(ulong pc) => Info?.FunctionAt(pc);
        public LineRow? LineAt(ulong pc) => Info?.LineAt(pc);

        public List<VarSlot> Locals(DwarfFunction fn, XContext ctx)
        {
            var outv = new List<VarSlot>();
            ulong frameBase = EvalFrameBase(fn.FrameBase, ctx);
            foreach (var v in fn.Variables)
            {
                int size = Info != null && v.TypeOffset != 0 ? Info.SizeOf(v.TypeOffset) : SizeOf(v.TypeName);
                var (addr, reg) = EvalLocation(v.Location, frameBase, ctx);
                outv.Add(new VarSlot(v.Name, v.TypeName, size, addr, reg, v.TypeOffset));
            }
            return outv;
        }

        private static bool HasDwarfLines(DwarfInfo info)
        {
            foreach (var u in info.Units)
                if (u.Lines.Count > 0) return true;
            return false;
        }

        private static ulong EvalFrameBase(byte[] loc, XContext ctx)
        {
            if (loc.Length == 0) return ctx.Gpr[1];
            byte op = loc[0];
            if (op >= 0x50 && op <= 0x6f) return ctx.Gpr[op - 0x50];
            if (op == 0x9c) return ctx.Gpr[1];
            if (op >= 0x70 && op <= 0x8f)
            {
                var c = new ByteReader(loc, 1);
                return (ulong)((long)ctx.Gpr[op - 0x70] + c.SLeb());
            }
            return ctx.Gpr[1];
        }

        private static (uint? addr, int? reg) EvalLocation(byte[] loc, ulong frameBase, XContext ctx)
        {
            if (loc.Length == 0) return (null, null);
            byte op = loc[0];
            if (op == 0x91)
            {
                var c = new ByteReader(loc, 1);
                return ((uint)((long)frameBase + c.SLeb()), null);
            }
            if (op == 0x03 && loc.Length >= 5)
            {
                var c = new ByteReader(loc, 1);
                return (c.U32BE(), null);
            }
            if (op >= 0x50 && op <= 0x6f) return (null, op - 0x50);
            if (op >= 0x70 && op <= 0x8f)
            {
                var c = new ByteReader(loc, 1);
                return ((uint)((long)ctx.Gpr[op - 0x70] + c.SLeb()), null);
            }
            return (null, null);
        }

        public static int SizeOf(string type)
        {
            string t = type.Replace("const ", "").Trim();
            if (t.EndsWith("*")) return 4;
            return t switch
            {
                "char" or "signed char" or "unsigned char" or "_Bool" or "bool" => 1,
                "short" or "short int" or "unsigned short" => 2,
                "long long" or "long long int" or "unsigned long long" or "double" => 8,
                _ => 4,
            };
        }

        private static bool Match(string want, string have) =>
            want.Equals(have, StringComparison.OrdinalIgnoreCase) ||
            have.EndsWith(want, StringComparison.OrdinalIgnoreCase) ||
            have.EndsWith("\\" + want, StringComparison.OrdinalIgnoreCase) ||
            have.EndsWith("/" + want, StringComparison.OrdinalIgnoreCase);

        private sealed class ByteReader
        {
            private readonly byte[] _b; private int _p;
            public ByteReader(byte[] b, int p) { _b = b; _p = p; }
            public long SLeb()
            {
                long r = 0; int s = 0; byte x;
                do { x = _b[_p++]; r |= (long)(x & 0x7f) << s; s += 7; } while ((x & 0x80) != 0);
                if (s < 64 && (x & 0x40) != 0) r |= -1L << s;
                return r;
            }
            public uint U32BE() => (uint)((_b[_p] << 24) | (_b[_p + 1] << 16) | (_b[_p + 2] << 8) | _b[_p + 3]);
        }
    }
}
