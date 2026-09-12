// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
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
        public VarSlot(string name, string type, int size, uint? addr, int? reg)
        { Name = name; TypeName = type; Size = size; Address = addr; Register = reg; }
    }

    /// <summary>
    /// The symbol side of the adapter: it turns a title's DWARF into the queries the
    /// DAP handlers make - resolve a source breakpoint to an address, map a stopped
    /// PC to a source frame, and lay out a function's locals against the stopped
    /// register context. Pure symbol logic (no devkit), so it is unit-testable
    /// offline against a Debug .elf.
    /// </summary>
    public sealed class Symbols
    {
        public readonly DwarfInfo Info;
        public Symbols(DwarfInfo info) { Info = info; }
        public static Symbols FromElf(string elfPath) => new(DwarfReader.Read(elfPath));

        /// <summary>Lowest address for a source file:line, snapping up to the next line
        /// that actually has code if the exact line has none.</summary>
        public (ulong address, int line)? ResolveBreakpoint(string file, int line)
        {
            var exact = Info.AddressFor(file, line);
            if (exact != null) return (exact.Value, line);
            // Snap to the next line at or after the requested one that has code.
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

        public DwarfFunction? FunctionAt(ulong pc) => Info.FunctionAt(pc);
        public LineRow? LineAt(ulong pc) => Info.LineAt(pc);

        /// <summary>Lay out the locals/params of the function at the stopped PC against
        /// the register context, computing each variable's guest address.</summary>
        public List<VarSlot> Locals(DwarfFunction fn, XContext ctx)
        {
            var outv = new List<VarSlot>();
            ulong frameBase = EvalFrameBase(fn.FrameBase, ctx);
            foreach (var v in fn.Variables)
            {
                int size = SizeOf(v.TypeName);
                var (addr, reg) = EvalLocation(v.Location, frameBase, ctx);
                outv.Add(new VarSlot(v.Name, v.TypeName, size, addr, reg));
            }
            return outv;
        }

        // frame_base is typically DW_OP_regN (the frame pointer) or DW_OP_call_frame_cfa.
        private static ulong EvalFrameBase(byte[] loc, XContext ctx)
        {
            if (loc.Length == 0) return ctx.Gpr[1];
            byte op = loc[0];
            if (op >= 0x50 && op <= 0x6f) return ctx.Gpr[op - 0x50];            // DW_OP_reg0..31
            if (op == 0x9c) return ctx.Gpr[1];                                  // DW_OP_call_frame_cfa ~= r1 (sp)
            if (op >= 0x70 && op <= 0x8f)                                       // DW_OP_breg0..31
            {
                var c = new ByteReader(loc, 1);
                return (ulong)((long)ctx.Gpr[op - 0x70] + c.SLeb());
            }
            return ctx.Gpr[1];
        }

        // Location expressions the compiler emits for a title's locals.
        private static (uint? addr, int? reg) EvalLocation(byte[] loc, ulong frameBase, XContext ctx)
        {
            if (loc.Length == 0) return (null, null);
            byte op = loc[0];
            if (op == 0x91)                                                     // DW_OP_fbreg <sleb>
            {
                var c = new ByteReader(loc, 1);
                return ((uint)((long)frameBase + c.SLeb()), null);
            }
            if (op == 0x03 && loc.Length >= 5)                                  // DW_OP_addr <u32>
            {
                var c = new ByteReader(loc, 1);
                return (c.U32BE(), null);
            }
            if (op >= 0x50 && op <= 0x6f) return (null, op - 0x50);            // DW_OP_reg0..31
            if (op >= 0x70 && op <= 0x8f)                                       // DW_OP_breg0..31
            {
                var c = new ByteReader(loc, 1);
                return ((uint)((long)ctx.Gpr[op - 0x70] + c.SLeb()), null);
            }
            return (null, null);
        }

        public static int SizeOf(string type)
        {
            string t = type.Replace("const ", "").Trim();
            if (t.EndsWith("*")) return 4;                                      // PPC32 pointer
            return t switch
            {
                "char" or "signed char" or "unsigned char" or "_Bool" or "bool" => 1,
                "short" or "short int" or "unsigned short" => 2,
                "long long" or "long long int" or "unsigned long long" or "double" => 8,
                _ => 4,                                                          // int/unsigned/long/float/enum
            };
        }

        private static bool Match(string want, string have) =>
            want.Equals(have, StringComparison.OrdinalIgnoreCase) ||
            have.EndsWith(want, StringComparison.OrdinalIgnoreCase) ||
            have.EndsWith("\\" + want, StringComparison.OrdinalIgnoreCase) ||
            have.EndsWith("/" + want, StringComparison.OrdinalIgnoreCase);

        /// <summary>Minimal SLEB/BE reader for decoding location bytes.</summary>
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
