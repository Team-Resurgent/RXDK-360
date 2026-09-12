// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// A DWARF v4 reader for RXDK-360 modern titles: it turns the .debug_* sections of
// a linked PowerPC big-endian ELF into functions, a line table and locals - the
// symbol backbone the devkit debugger maps onto the running XEX. Scope is what a
// stepping debugger needs (line program + subprograms + variables + basic types),
// not a full DWARF consumer.

using System;
using System.Collections.Generic;

namespace Rxdk.Xbox360.Dwarf
{
    public sealed class DwarfReader
    {
        private sealed class Abbrev
        {
            public int Tag;
            public bool HasChildren;
            public readonly List<(int attr, int form, long implicit_)> Attrs = new();
        }

        private sealed class Die
        {
            public int Tag;
            public ulong Offset;                 // absolute offset in .debug_info
            public readonly Dictionary<int, object> Attrs = new();
            public readonly Dictionary<int, int> Forms = new();
            public readonly List<Die> Children = new();

            public string Str(int at) => Attrs.TryGetValue(at, out var v) ? v as string ?? "" : "";
            public bool Has(int at) => Attrs.ContainsKey(at);
            public ulong U(int at) => Attrs.TryGetValue(at, out var v) && v is ulong u ? u : 0;
            public byte[] Blk(int at) => Attrs.TryGetValue(at, out var v) ? v as byte[] ?? Array.Empty<byte>() : Array.Empty<byte>();
        }

        private readonly byte[] _info, _abbrev, _str, _lineStr, _line;
        private readonly Dictionary<ulong, Die> _byOffset = new();

        private DwarfReader(ElfFile elf)
        {
            _info = elf.Section(".debug_info");
            _abbrev = elf.Section(".debug_abbrev");
            _str = elf.Section(".debug_str");
            _lineStr = elf.Section(".debug_line_str");
            _line = elf.Section(".debug_line");
        }

        public static DwarfInfo Read(string elfPath) => Read(ElfFile.Load(elfPath));

        public static DwarfInfo Read(ElfFile elf)
        {
            var r = new DwarfReader(elf);
            var info = new DwarfInfo();
            if (r._info.Length == 0) return info;
            var cur = new ByteCursor(r._info);
            while (cur.Pos + 11 <= r._info.Length)
            {
                var cu = r.ReadCompileUnit(cur);
                if (cu != null) info.Units.Add(cu);
            }
            return info;
        }

        private CompileUnit? ReadCompileUnit(ByteCursor cur)
        {
            int cuStart = cur.Pos;
            uint unitLength = cur.U32();
            if (unitLength == 0 || unitLength >= 0xfffffff0u) return null;   // 64-bit DWARF not emitted
            int next = cur.Pos + (int)unitLength;
            ushort version = cur.U16();
            uint abbrevOff; int addrSize;
            if (version >= 5)
            {
                cur.U8();                       // unit_type
                addrSize = cur.U8();
                abbrevOff = cur.U32();
            }
            else
            {
                abbrevOff = cur.U32();
                addrSize = cur.U8();
            }

            var abbrevs = ParseAbbrev(abbrevOff);
            var root = ReadDie(cur, abbrevs, cuStart, addrSize, next);
            cur.Pos = next;                     // resume at the next CU regardless
            if (root == null || root.Tag != DW_TAG.compile_unit) return null;

            var unit = new CompileUnit { Name = root.Str(DW_AT.name), CompDir = root.Str(DW_AT.comp_dir) };

            // Line table (address <-> source).
            List<string> files = new();
            if (root.Has(DW_AT.stmt_list))
                ParseLineProgram((int)root.U(DW_AT.stmt_list), addrSize, unit, files);

            // Functions + their variables.
            foreach (var die in Flatten(root))
            {
                if (die.Tag != DW_TAG.subprogram || !die.Has(DW_AT.low_pc)) continue;
                ulong low = die.U(DW_AT.low_pc);
                ulong high = die.U(DW_AT.high_pc);
                // high_pc is an address if its form is DW_FORM_addr, else an offset.
                if (die.Forms.TryGetValue(DW_AT.high_pc, out int hf) && hf != DW_FORM.addr) high = low + high;
                var fn = new DwarfFunction
                {
                    Name = die.Str(DW_AT.name),
                    LowPc = low,
                    HighPc = high,
                    DeclLine = (int)die.U(DW_AT.decl_line),
                    FrameBase = die.Blk(DW_AT.frame_base),
                    File = FileName(files, (int)die.U(DW_AT.decl_file)),
                };
                CollectVariables(die, fn, files);
                unit.Functions.Add(fn);
            }
            return unit;
        }

        private void CollectVariables(Die scope, DwarfFunction fn, List<string> files)
        {
            foreach (var child in scope.Children)
            {
                if (child.Tag == DW_TAG.variable || child.Tag == DW_TAG.formal_parameter)
                {
                    fn.Variables.Add(new DwarfVariable
                    {
                        Name = child.Str(DW_AT.name),
                        IsParameter = child.Tag == DW_TAG.formal_parameter,
                        DeclLine = (int)child.U(DW_AT.decl_line),
                        Location = child.Blk(DW_AT.location),
                        TypeName = child.Has(DW_AT.type) ? TypeName(child.U(DW_AT.type), 0) : "",
                    });
                }
                else if (child.Tag == DW_TAG.lexical_block)
                {
                    CollectVariables(child, fn, files);   // locals in nested scopes
                }
            }
        }

        private string TypeName(ulong offset, int depth)
        {
            if (depth > 16 || !_byOffset.TryGetValue(offset, out var t)) return "";
            switch (t.Tag)
            {
                case DW_TAG.base_type:
                case DW_TAG.typedef:
                    return t.Str(DW_AT.name);
                case DW_TAG.pointer_type:
                    return (t.Has(DW_AT.type) ? TypeName(t.U(DW_AT.type), depth + 1) : "void") + "*";
                case DW_TAG.const_type:
                    return "const " + (t.Has(DW_AT.type) ? TypeName(t.U(DW_AT.type), depth + 1) : "void");
                case DW_TAG.structure_type:
                    return "struct " + t.Str(DW_AT.name);
                default:
                    return t.Str(DW_AT.name);
            }
        }

        private static IEnumerable<Die> Flatten(Die d)
        {
            yield return d;
            foreach (var c in d.Children)
                foreach (var g in Flatten(c))
                    yield return g;
        }

        // ---- DIE / abbrev -----------------------------------------------------

        private Dictionary<ulong, Abbrev> ParseAbbrev(uint offset)
        {
            var map = new Dictionary<ulong, Abbrev>();
            if (offset >= _abbrev.Length) return map;
            var c = new ByteCursor(_abbrev, (int)offset);
            while (!c.AtEnd)
            {
                ulong code = c.ULeb();
                if (code == 0) break;
                var a = new Abbrev { Tag = (int)c.ULeb(), HasChildren = c.U8() != 0 };
                while (true)
                {
                    int at = (int)c.ULeb();
                    int form = (int)c.ULeb();
                    long impl = 0;
                    if (form == DW_FORM.implicit_const) impl = c.SLeb();
                    if (at == 0 && form == 0) break;
                    a.Attrs.Add((at, form, impl));
                }
                map[code] = a;
            }
            return map;
        }

        private Die? ReadDie(ByteCursor cur, Dictionary<ulong, Abbrev> abbrevs,
                             int cuStart, int addrSize, int cuEnd)
        {
            ulong offset = (ulong)cur.Pos;
            ulong code = cur.ULeb();
            if (code == 0) return null;                         // null DIE (sibling terminator)
            if (!abbrevs.TryGetValue(code, out var abbrev)) return null;

            var die = new Die { Tag = abbrev.Tag, Offset = offset };
            foreach (var (at, form, impl) in abbrev.Attrs)
            {
                object val = ReadForm(cur, form, addrSize, cuStart, impl);
                if (at != 0)
                {
                    die.Attrs[at] = val;
                    die.Forms[at] = form;
                }
            }
            _byOffset[offset] = die;

            if (abbrev.HasChildren)
            {
                while (cur.Pos < cuEnd)
                {
                    var child = ReadDie(cur, abbrevs, cuStart, addrSize, cuEnd);
                    if (child == null) break;
                    die.Children.Add(child);
                }
            }
            return die;
        }

        private object ReadForm(ByteCursor c, int form, int addrSize, int cuStart, long impl)
        {
            switch (form)
            {
                case DW_FORM.addr: return c.UPtr(addrSize);
                case DW_FORM.data1: case DW_FORM.ref1: return form == DW_FORM.ref1 ? (ulong)cuStart + c.U8() : (ulong)c.U8();
                case DW_FORM.data2: case DW_FORM.ref2: return form == DW_FORM.ref2 ? (ulong)cuStart + c.U16() : (ulong)c.U16();
                case DW_FORM.data4: case DW_FORM.ref4: return form == DW_FORM.ref4 ? (ulong)cuStart + c.U32() : (ulong)c.U32();
                case DW_FORM.data8: case DW_FORM.ref8: return form == DW_FORM.ref8 ? (ulong)cuStart + c.U64() : c.U64();
                case DW_FORM.ref_udata: return (ulong)cuStart + c.ULeb();
                case DW_FORM.ref_addr: return (ulong)c.U32();
                case DW_FORM.sec_offset: return (ulong)c.U32();
                case DW_FORM.udata: return c.ULeb();
                case DW_FORM.sdata: return unchecked((ulong)c.SLeb());
                case DW_FORM.@string: return c.CStr();
                case DW_FORM.strp: return StrSection.At(_str, c.U32());
                case DW_FORM.line_strp: return StrSection.At(_lineStr, c.U32());
                case DW_FORM.flag: return (ulong)c.U8();
                case DW_FORM.flag_present: return (ulong)1;
                case DW_FORM.implicit_const: return unchecked((ulong)impl);
                case DW_FORM.exprloc: { int n = (int)c.ULeb(); return c.Bytes(n); }
                case DW_FORM.block1: { int n = c.U8(); return c.Bytes(n); }
                case DW_FORM.block2: { int n = c.U16(); return c.Bytes(n); }
                case DW_FORM.block4: { int n = (int)c.U32(); return c.Bytes(n); }
                case DW_FORM.block: { int n = (int)c.ULeb(); return c.Bytes(n); }
                case DW_FORM.data16: return c.Bytes(16);
                case DW_FORM.strx1: case DW_FORM.addrx1: c.U8(); return "";
                case DW_FORM.strx2: case DW_FORM.addrx2: c.U16(); return "";
                case DW_FORM.strx3: case DW_FORM.addrx3: c.Skip(3); return "";
                case DW_FORM.strx4: case DW_FORM.addrx4: c.U32(); return "";
                case DW_FORM.strx: case DW_FORM.addrx: c.ULeb(); return "";
                case DW_FORM.indirect: return ReadForm(c, (int)c.ULeb(), addrSize, cuStart, impl);
                default: throw new NotSupportedException($"DW_FORM 0x{form:X}");
            }
        }

        // ---- line-number program (DWARF v2-4) ---------------------------------

        private void ParseLineProgram(int offset, int addrSize, CompileUnit unit, List<string> filesOut)
        {
            if (offset >= _line.Length) return;
            var c = new ByteCursor(_line, offset);
            uint unitLen = c.U32();
            int end = c.Pos + (int)unitLen;
            ushort ver = c.U16();
            if (ver >= 5) { c.U8(); c.U8(); }              // v5 address_size + segment_selector_size
            uint headerLen = c.U32();
            int programStart = c.Pos + (int)headerLen;
            int minInst = c.U8();
            int maxOps = ver >= 4 ? c.U8() : 1;
            bool defaultIsStmt = c.U8() != 0;
            sbyte lineBase = (sbyte)c.U8();
            int lineRange = c.U8();
            int opcodeBase = c.U8();
            var stdLens = new int[opcodeBase];
            for (int i = 1; i < opcodeBase; i++) stdLens[i] = c.U8();

            // v2-4 directory + file tables (v5 uses a different format we don't emit).
            var dirs = new List<string> { unit.CompDir };
            if (ver < 5)
            {
                while (true) { string d = c.CStr(); if (d.Length == 0) break; dirs.Add(d); }
                filesOut.Add("");                          // file index is 1-based
                while (true)
                {
                    string name = c.CStr();
                    if (name.Length == 0) break;
                    int dirIdx = (int)c.ULeb(); c.ULeb(); c.ULeb();   // dir, mtime, size
                    filesOut.Add(Combine(dirs, dirIdx, name));
                }
            }

            // Run the line-number state machine.
            c.Pos = programStart;
            ulong address = 0; int file = 1, line = 1, column = 0; bool isStmt = defaultIsStmt, endSeq = false;
            void Emit()
            {
                unit.Lines.Add(new LineRow
                {
                    Address = address,
                    File = FileName(filesOut, file),
                    Line = line,
                    Column = column,
                    IsStmt = isStmt,
                    EndSequence = endSeq,
                });
            }
            void Reset() { address = 0; file = 1; line = 1; column = 0; isStmt = defaultIsStmt; endSeq = false; }

            while (c.Pos < end)
            {
                int op = c.U8();
                if (op == 0)                                // extended opcode
                {
                    int len = (int)c.ULeb();
                    int opEnd = c.Pos + len;
                    int sub = c.U8();
                    switch (sub)
                    {
                        case DW_LNE.end_sequence: endSeq = true; Emit(); Reset(); break;
                        case DW_LNE.set_address: address = c.UPtr(addrSize); break;
                        default: break;                     // define_file / set_discriminator / vendor
                    }
                    c.Pos = opEnd;
                }
                else if (op < opcodeBase)                   // standard opcode
                {
                    switch (op)
                    {
                        case DW_LNS.copy: Emit(); break;
                        case DW_LNS.advance_pc: address += (ulong)c.ULeb() * (ulong)minInst; break;
                        case DW_LNS.advance_line: line += (int)c.SLeb(); break;
                        case DW_LNS.set_file: file = (int)c.ULeb(); break;
                        case DW_LNS.set_column: column = (int)c.ULeb(); break;
                        case DW_LNS.negate_stmt: isStmt = !isStmt; break;
                        case DW_LNS.set_basic_block: break;
                        case DW_LNS.const_add_pc: address += (ulong)((255 - opcodeBase) / lineRange) * (ulong)minInst; break;
                        case DW_LNS.fixed_advance_pc: address += c.U16(); break;
                        case DW_LNS.set_prologue_end: case DW_LNS.set_epilogue_begin: break;
                        case DW_LNS.set_isa: c.ULeb(); break;
                        default: for (int i = 0; i < stdLens[op]; i++) c.ULeb(); break;  // unknown: skip operands
                    }
                }
                else                                        // special opcode
                {
                    int adj = op - opcodeBase;
                    address += (ulong)(adj / lineRange) * (ulong)minInst;
                    line += lineBase + (adj % lineRange);
                    Emit();
                }
            }
        }

        private static string Combine(List<string> dirs, int dirIdx, string name)
        {
            if (name.Length > 1 && (name[1] == ':' || name[0] == '/' || name[0] == '\\')) return name; // absolute
            string dir = dirIdx >= 0 && dirIdx < dirs.Count ? dirs[dirIdx] : "";
            return dir.Length == 0 ? name : dir.TrimEnd('/', '\\') + "\\" + name;
        }

        private static string FileName(List<string> files, int index) =>
            index > 0 && index < files.Count ? files[index] : "";
    }
}
