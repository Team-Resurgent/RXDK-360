// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System.Collections.Generic;

namespace Rxdk.Xbox360.Dwarf
{
    /// <summary>One row of the line-number table: a guest address and the source
    /// position that starts there.</summary>
    public sealed class LineRow
    {
        public ulong Address;
        public string File = "";
        public int Line;
        public int Column;
        public bool IsStmt;
        public bool EndSequence;
        public override string ToString() =>
            $"0x{Address:X8}  {File}:{Line}" + (Column != 0 ? $":{Column}" : "") +
            (EndSequence ? "  (end)" : IsStmt ? "  (stmt)" : "");
    }

    /// <summary>A local variable or parameter of a function.</summary>
    public sealed class DwarfVariable
    {
        public string Name = "";
        public string TypeName = "";
        public int DeclLine;
        public bool IsParameter;
        /// <summary>Raw DWARF location expression (e.g. DW_OP_fbreg &lt;offset&gt;).</summary>
        public byte[] Location = System.Array.Empty<byte>();
        public override string ToString() =>
            (IsParameter ? "param " : "local ") + TypeName + " " + Name + LocationSummary.Of(Location);
    }

    /// <summary>A function: its address range and source position.</summary>
    public sealed class DwarfFunction
    {
        public string Name = "";
        public ulong LowPc;
        public ulong HighPc;   // exclusive end
        public string File = "";
        public int DeclLine;
        public byte[] FrameBase = System.Array.Empty<byte>();
        public readonly List<DwarfVariable> Variables = new();
        public bool Contains(ulong addr) => addr >= LowPc && addr < HighPc;
        public override string ToString() =>
            $"0x{LowPc:X8}-0x{HighPc:X8}  {Name}  ({File}:{DeclLine})";
    }

    /// <summary>One compilation unit.</summary>
    public sealed class CompileUnit
    {
        public string Name = "";
        public string CompDir = "";
        public readonly List<DwarfFunction> Functions = new();
        public readonly List<LineRow> Lines = new();
    }

    /// <summary>The parsed DWARF for a title: units, functions and the line table,
    /// with the address/source queries a debugger needs.</summary>
    public sealed class DwarfInfo
    {
        public readonly List<CompileUnit> Units = new();

        public IEnumerable<DwarfFunction> Functions
        {
            get { foreach (var u in Units) foreach (var f in u.Functions) yield return f; }
        }

        /// <summary>The function whose range contains <paramref name="addr"/>, or null.</summary>
        public DwarfFunction? FunctionAt(ulong addr)
        {
            foreach (var f in Functions)
                if (f.LowPc != 0 && f.Contains(addr)) return f;
            return null;
        }

        /// <summary>The source position for a guest address (the last line row at or
        /// before it within its sequence), or null.</summary>
        public LineRow? LineAt(ulong addr)
        {
            LineRow? best = null;
            foreach (var u in Units)
            {
                for (int i = 0; i < u.Lines.Count; i++)
                {
                    var r = u.Lines[i];
                    if (r.EndSequence) continue;
                    // A row covers [r.Address, nextRow.Address).
                    ulong end = (i + 1 < u.Lines.Count) ? u.Lines[i + 1].Address : r.Address + 1;
                    if (addr >= r.Address && addr < end)
                        if (best == null || r.Address >= best.Address) best = r;
                }
            }
            return best;
        }

        /// <summary>The lowest address mapped to a given file:line (for setting a
        /// breakpoint), or null.</summary>
        public ulong? AddressFor(string file, int line)
        {
            ulong? best = null;
            foreach (var u in Units)
                foreach (var r in u.Lines)
                {
                    if (r.EndSequence || r.Line != line) continue;
                    if (!file.Equals(r.File, System.StringComparison.OrdinalIgnoreCase) &&
                        !r.File.EndsWith(file, System.StringComparison.OrdinalIgnoreCase)) continue;
                    if (best == null || r.Address < best) best = r.Address;
                }
            return best;
        }
    }

    internal static class LocationSummary
    {
        // A short human-readable decode of the common location forms, for dumps.
        public static string Of(byte[] loc)
        {
            if (loc.Length == 0) return "";
            byte op = loc[0];
            // DW_OP_fbreg = 0x91 (SLEB offset from the frame base)
            if (op == 0x91 && loc.Length > 1)
            {
                var c = new ByteCursor(loc, 1);
                return $"  [fbreg {c.SLeb()}]";
            }
            // DW_OP_addr = 0x03 (a fixed address)
            if (op == 0x03 && loc.Length >= 5)
            {
                var c = new ByteCursor(loc, 1);
                return $"  [addr 0x{c.U32():X8}]";
            }
            // DW_OP_reg0..31 = 0x50..0x6f
            if (op >= 0x50 && op <= 0x6f) return $"  [reg {op - 0x50}]";
            // DW_OP_breg0..31 = 0x70..0x8f (SLEB offset)
            if (op >= 0x70 && op <= 0x8f && loc.Length > 1)
            {
                var c = new ByteCursor(loc, 1);
                return $"  [breg{op - 0x70} {c.SLeb()}]";
            }
            return $"  [op 0x{op:X2}]";
        }
    }
}
