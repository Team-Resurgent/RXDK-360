// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Globalization;
using Rxdk.Xbox360.Dwarf;
using Rxdk.Xbox360.Xbdm;

namespace Rxdk.Xbox360.DebugAdapter;

/// <summary>
/// DWARF locals / hover for clang titles: type size and encoding from the DIE,
/// expandable structs/arrays like the PDB <see cref="ManagedValues"/> path.
/// </summary>
internal sealed class DwarfValues
{
    private readonly Symbols _sym;
    private readonly DwarfInfo _info;
    private XContext _ctx;

    public DwarfValues(Symbols symbols, XContext ctx)
    {
        _sym = symbols;
        _info = symbols.Info ?? throw new ArgumentException("DWARF symbols required", nameof(symbols));
        _ctx = ctx;
    }

    public bool EmitLocals(ValueList variables, KitMemory memory)
    {
        var fn = _sym.FunctionAt(_ctx.Iar);
        if (fn == null)
            return false;
        foreach (var slot in _sym.Locals(fn, _ctx))
            EmitSlot(slot, variables, memory);
        return true;
    }

    public bool TryEvaluate(string expression, KitMemory memory, out string value, out bool expandable, out string expandKey)
    {
        value = "";
        expandable = false;
        expandKey = "";
        var fn = _sym.FunctionAt(_ctx.Iar);
        if (fn == null)
            return false;
        foreach (var slot in _sym.Locals(fn, _ctx))
        {
            if (!string.Equals(slot.Name, expression, StringComparison.Ordinal) &&
                !string.Equals(slot.Name, expression, StringComparison.OrdinalIgnoreCase))
                continue;
            DescribeSlot(slot, memory, out value, out expandable, out expandKey);
            return true;
        }
        return false;
    }

    public bool TryEmitMembers(string expandKey, ValueList variables, KitMemory memory)
    {
        if (!TryParseRef(expandKey, out var address, out var typeOffset))
            return false;
        EmitChildren(typeOffset, address, variables, memory);
        return true;
    }

    private void EmitSlot(VarSlot slot, ValueList variables, KitMemory memory)
    {
        DescribeSlot(slot, memory, out var value, out var expandable, out var key);
        variables.Append(slot.Name, value, slot.TypeName, expandable, key);
    }

    private void DescribeSlot(VarSlot slot, KitMemory memory, out string value, out bool expandable, out string expandKey)
    {
        expandable = false;
        expandKey = "";
        var type = slot.TypeOffset != 0 ? _info.TypeOf(slot.TypeOffset) : null;
        if (slot.Address is uint addr)
        {
            expandable = _info.IsExpandable(type);
            if (expandable)
                expandKey = AddrRef(addr, slot.TypeOffset);
            value = Describe(slot.TypeOffset, addr, memory, slot.TypeName);
            return;
        }
        if (slot.Register is int reg && _ctx.Gpr != null && reg >= 0 && reg < _ctx.Gpr.Length)
        {
            value = FormatRaw(type, slot.TypeName, _ctx.Gpr[reg], slot.Size);
            return;
        }
        value = "<optimized out>";
    }

    private void EmitChildren(ulong typeOffset, uint address, ValueList variables, KitMemory memory)
    {
        var type = _info.TypeOf(typeOffset);
        if (type == null || variables.IsFull)
            return;

        if (type.IsPointer)
        {
            var ptr = memory.ReadDword(address);
            if (ptr is null or 0)
                return;
            EmitChildren(type.ReferentOffset, ptr.Value, variables, memory);
            return;
        }

        if (type.IsArray)
        {
            int count = type.ArrayCount > 0 ? type.ArrayCount : 0;
            if (count <= 0)
                return;
            if (count > 32)
                count = 32;
            int elemSize = _info.SizeOf(type.ReferentOffset);
            if (elemSize <= 0)
                elemSize = 4;
            for (int i = 0; i < count && !variables.IsFull; i++)
            {
                uint elemAddr = address + (uint)(i * elemSize);
                var elem = _info.TypeOf(type.ReferentOffset);
                bool exp = _info.IsExpandable(elem);
                variables.Append(
                    $"[{i}]",
                    Describe(type.ReferentOffset, elemAddr, memory, _info.DisplayName(type.ReferentOffset)),
                    _info.DisplayName(type.ReferentOffset),
                    exp,
                    exp ? AddrRef(elemAddr, type.ReferentOffset) : "");
            }
            return;
        }

        foreach (var m in type.Members)
        {
            if (variables.IsFull)
                return;
            uint memberAddr = address + (uint)m.Offset;
            var mt = _info.TypeOf(m.TypeOffset);
            if (string.IsNullOrEmpty(m.Name) && mt != null && (mt.IsStruct || mt.IsArray))
            {
                EmitChildren(m.TypeOffset, memberAddr, variables, memory);
                continue;
            }
            if (string.IsNullOrEmpty(m.Name))
                continue;
            bool exp = _info.IsExpandable(mt);
            variables.Append(
                m.Name,
                Describe(m.TypeOffset, memberAddr, memory, _info.DisplayName(m.TypeOffset)),
                _info.DisplayName(m.TypeOffset),
                exp,
                exp ? AddrRef(memberAddr, m.TypeOffset) : "");
        }
    }

    private string Describe(ulong typeOffset, uint address, KitMemory memory, string fallbackName)
    {
        var type = _info.TypeOf(typeOffset);
        if (type == null)
            return FormatNamed(fallbackName, memory.ReadSized(address, 4) ?? 0, 4);

        if (type.IsPointer)
        {
            var ptr = memory.ReadDword(address);
            if (ptr is null)
                return "<unreadable>";
            return $"0x{ptr.Value:x8}";
        }

        if (type.IsArray || type.IsStruct)
        {
            int size = _info.SizeOf(type);
            string name = !string.IsNullOrEmpty(fallbackName) ? fallbackName : _info.DisplayName(typeOffset);
            if (string.IsNullOrEmpty(name))
                name = $"{{{size} bytes}}";
            return name;
        }

        if (type.IsFloat && type.ByteSize == 8)
        {
            var bits = memory.ReadQword(address);
            if (bits is null)
                return "<unreadable>";
            return $"{BitConverter.Int64BitsToDouble((long)bits.Value):g}";
        }

        if (type.IsFloat)
        {
            var bits = memory.ReadDword(address);
            if (bits is null)
                return "<unreadable>";
            return $"{BitConverter.Int32BitsToSingle((int)bits.Value):g}";
        }

        int sz = _info.SizeOf(type);
        if (sz == 8)
        {
            var q = memory.ReadQword(address);
            return q is null ? "<unreadable>" : q.Value.ToString(CultureInfo.InvariantCulture);
        }
        var raw = memory.ReadSized(address, sz <= 0 ? 4u : (uint)sz);
        if (raw is null)
            return "<unreadable>";
        return FormatNamed(
            !string.IsNullOrEmpty(fallbackName) ? fallbackName : _info.DisplayName(typeOffset),
            raw.Value, sz);
    }

    private static string FormatRaw(DwarfType? type, string typeName, ulong raw, int size)
    {
        if (type != null && type.IsFloat && size == 8)
            return $"{BitConverter.Int64BitsToDouble((long)raw):g}";
        if (type != null && type.IsFloat)
            return $"{BitConverter.Int32BitsToSingle((int)raw):g}";
        return FormatNamed(typeName, raw, size);
    }

    private static string FormatNamed(string type, ulong raw, int size)
    {
        string t = (type ?? "").Replace("const ", "").Trim();
        if (t.EndsWith("*", StringComparison.Ordinal))
            return $"0x{raw:X8}";
        if (t is "float")
            return $"{BitConverter.Int32BitsToSingle((int)raw):g}";
        if (t is "double")
            return $"{BitConverter.Int64BitsToDouble((long)raw):g}";
        if (t is "char" or "signed char" or "unsigned char")
        {
            char ch = (char)(raw & 0xff);
            return char.IsControl(ch) ? ((long)raw).ToString(CultureInfo.InvariantCulture) : $"'{ch}' ({(long)raw})";
        }
        if (t is "_Bool" or "bool")
            return raw != 0 ? "true" : "false";
        bool unsigned = t.Contains("unsigned");
        if (!unsigned)
        {
            long sv = size switch
            {
                1 => (sbyte)raw,
                2 => (short)raw,
                4 => (int)raw,
                _ => (long)raw,
            };
            return sv.ToString(CultureInfo.InvariantCulture);
        }
        return raw.ToString(CultureInfo.InvariantCulture);
    }

    internal static string AddrRef(uint address, ulong typeOffset) =>
        $"#dwarf#{address:x}#{typeOffset:x}";

    internal static bool TryParseRef(string s, out uint address, out ulong typeOffset)
    {
        address = 0;
        typeOffset = 0;
        const string prefix = "#dwarf#";
        if (s == null || !s.StartsWith(prefix, StringComparison.Ordinal))
            return false;
        var rest = s.Substring(prefix.Length);
        var hash = rest.IndexOf('#');
        if (hash < 1)
            return false;
        try
        {
            address = (uint)Convert.ToUInt64(rest.Substring(0, hash), 16);
            typeOffset = Convert.ToUInt64(rest.Substring(hash + 1), 16);
            return true;
        }
        catch
        {
            return false;
        }
    }
}
