// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using Rxdk.Xbox360.Pdb;
using Rxdk.Xbox360.Pdb.Symbols;
using Rxdk.Xbox360.Pdb.Tpi;
using Rxdk.Xbox360.Xbdm;

namespace Rxdk.Xbox360.DebugAdapter;

/// <summary>One Locals / hover / Watch row the DAP session can emit.</summary>
internal sealed class ValueRow
{
    public required string Name { get; init; }
    public required string Value { get; init; }
    public string Type { get; init; } = "";
    public bool Expandable { get; init; }
    public string ExpandKey { get; init; } = "";
}

internal sealed class ValueList
{
    private readonly HashSet<string> _emitted = new(StringComparer.Ordinal);
    public List<ValueRow> Rows { get; } = new();
    public int Count => Rows.Count;
    public bool IsFull => Rows.Count >= 256;
    public bool WasEmitted(string name) => _emitted.Contains(name);

    public void Append(string name, string value, string type, bool expandable, string expandKey)
    {
        if (!_emitted.Add(name))
            return;
        Rows.Add(new ValueRow
        {
            Name = name,
            Value = value,
            Type = type,
            Expandable = expandable,
            ExpandKey = expandable ? expandKey : "",
        });
    }
}

/// <summary>
/// PDB locals / hover / Watch for Xbox 360: frame addresses from PPC GPRs (r1 after
/// <c>stwu</c>), scalars and pointers read big-endian, structs and std containers
/// expanded the same way as the original-Xbox DAP.
/// </summary>
internal sealed class ManagedValues
{
    private readonly PdbImage _pdb;
    private readonly uint _moduleBase;

    public ManagedValues(PdbImage pdb, uint moduleBase)
    {
        _pdb = pdb;
        _moduleBase = moduleBase;
    }

    public bool EmitLocals(ref XContext context, ValueList variables, KitMemory memory)
    {
        var frame = FindFrame(context.Iar);
        if (frame is null)
            return false;

        var emitted = false;
        foreach (var local in frame.Locals)
        {
            if (variables.IsFull)
                break;
            if (IsHidden(local.Name) || variables.WasEmitted(local.Name))
                continue;

            var address = ResolveLocalAddress(frame, local, ref context);
            EmitValue(local.Name, local.TypeIndex, address, memory, variables, expandBase: local.Name);
            emitted = true;
        }

        return emitted;
    }

    public bool TryEmitMembers(string expression, ref XContext context, ValueList variables, KitMemory memory)
    {
        if (TryParseAddrRef(expression, out var refAddr, out var refType))
        {
            EmitChildren(refType, refAddr, memory, variables);
            return variables.Count > 0;
        }

        if (!ExpressionPath.TryParse(expression, out var baseName, out var accessors))
            return false;
        if (!TryResolveBase(baseName, ref context, memory, out var address, out var typeIndex))
            return false;

        if (accessors.Count > 0)
        {
            var evaluator = new TypeEvaluator(_pdb.Types, a => memory.ReadDword((uint)a));
            if (!evaluator.TryWalk(address, typeIndex, accessors, out var finalAddress, out var finalType, out _))
                return false;
            address = (uint)finalAddress;
            typeIndex = finalType.TypeIndex;
        }

        EmitChildren(typeIndex, address, memory, variables);
        return variables.Count > 0;
    }

    public bool TryEvaluate(string expression, ref XContext context, KitMemory memory, out string value, out string? error, out bool expandable)
    {
        value = string.Empty;
        error = null;
        expandable = false;

        if (!ExpressionPath.TryParse(expression, out var baseName, out var accessors))
        {
            error = "badExpression";
            return false;
        }

        if (accessors.Count == 0 && TryReadRegister(baseName, ref context, out var register))
        {
            value = $"0x{register:x8}";
            return true;
        }

        if (!TryResolveBase(baseName, ref context, memory, out var address, out var typeIndex))
        {
            error = "symbolNotFound";
            return false;
        }

        var evaluator = new TypeEvaluator(_pdb.Types, a => memory.ReadDword((uint)a));
        if (!evaluator.TryWalk(address, typeIndex, accessors, out var finalAddress, out var finalType, out var walkError))
        {
            error = walkError ?? "evalFailed";
            return false;
        }

        value = Describe(finalType, (uint)finalAddress, memory, out expandable);
        return true;
    }

    private FrameInfo? FindFrame(uint iar)
    {
        if (_moduleBase == 0 || iar < _moduleBase)
            return null;
        return _pdb.FindFrame(iar - _moduleBase);
    }

    private void EmitValue(string name, uint typeIndex, uint address, KitMemory memory, ValueList variables, string expandBase)
    {
        var type = _pdb.Types.Resolve(typeIndex);
        var label = Describe(type, address, memory, out var expandable);
        var typeName = type.Name ?? "";
        variables.Append(name, label, typeName, expandable, expandBase);
    }

    private string Describe(PdbType type, uint address, KitMemory memory, out bool expandable)
    {
        type = _pdb.Types.Peel(type.TypeIndex);
        switch (type.Kind)
        {
            case PdbTypeKind.Array:
            {
                var elem = type.ReferentType != 0 ? _pdb.Types.Resolve(type.ReferentType) : null;
                expandable = type.ElementCount > 0;
                return elem?.Name is { Length: > 0 } en ? $"{en}[{type.ElementCount}]" : $"array[{type.ElementCount}]";
            }

            case PdbTypeKind.Struct:
            case PdbTypeKind.Class:
            case PdbTypeKind.Union:
                if (TryFormatString(type, address, memory, out var sval))
                {
                    expandable = false;
                    return sval;
                }
                if (TryContainerSummary(type, address, memory, out var csummary, out expandable))
                    return csummary;
                expandable = type.Members.Count > 0;
                return type.Name is { Length: > 0 } tn ? tn : $"{{{type.ByteSize} bytes}}";

            case PdbTypeKind.Enum:
                expandable = false;
                return FormatEnum(type, address, memory);

            case PdbTypeKind.Pointer:
            {
                var referent = type.ReferentType != 0 ? _pdb.Types.Peel(type.ReferentType) : null;
                expandable = referent is not null &&
                    ((referent.IsAggregate && referent.Members.Count > 0) ||
                     (referent.Kind == PdbTypeKind.Array && referent.ElementCount > 0));
                return FormatPointer(type, address, memory);
            }

            default:
                expandable = false;
                return FormatScalar(type, address, memory);
        }
    }

    internal static uint ResolveLocalAddress(FrameInfo frame, LocalVariable local, ref XContext context)
    {
        var gpr = context.Gpr ?? Array.Empty<ulong>();
        uint G(int i) => i >= 0 && i < gpr.Length ? (uint)gpr[i] : 0;
        long baseValue = local.Base switch
        {
            FrameBase.Gpr => G(local.Register),
            FrameBase.Esp => G(1),
            FrameBase.VFrame => (G(1) - frame.CalleeSavedBytes) & ~0xFu,
            _ => G(1), // Ebp on this target is the PPC stack pointer
        };
        return (uint)(baseValue + local.FrameOffset);
    }

    private bool TryResolveThisMember(
        string name, FrameInfo frame, ref XContext context, KitMemory memory,
        out uint address, out uint typeIndex)
    {
        address = 0;
        typeIndex = 0;

        var thisLocal = frame.Locals.FirstOrDefault(l => string.Equals(l.Name, "this", StringComparison.Ordinal));
        if (thisLocal is null)
            return false;

        var thisPtr = memory.ReadDword(ResolveLocalAddress(frame, thisLocal, ref context));
        if (thisPtr is null or 0)
            return false;

        var pointerType = _pdb.Types.Peel(thisLocal.TypeIndex);
        if (pointerType.Kind != PdbTypeKind.Pointer || pointerType.ReferentType == 0)
            return false;

        if (!_pdb.Types.TryFindMember(pointerType.ReferentType, name, out var offset, out var memberType))
            return false;

        address = thisPtr.Value + offset;
        typeIndex = memberType;
        return true;
    }

    private bool TryResolveBase(string name, ref XContext context, KitMemory memory, out uint address, out uint typeIndex)
    {
        address = 0;
        typeIndex = 0;

        var frame = FindFrame(context.Iar);
        if (frame is not null)
        {
            var local = frame.Locals.FirstOrDefault(l => string.Equals(l.Name, name, StringComparison.Ordinal))
                        ?? frame.Locals.FirstOrDefault(l => string.Equals(l.Name, name, StringComparison.OrdinalIgnoreCase));
            if (local is not null)
            {
                address = ResolveLocalAddress(frame, local, ref context);
                typeIndex = local.TypeIndex;
                return true;
            }

            if (TryResolveThisMember(name, frame, ref context, memory, out address, out typeIndex))
                return true;
        }

        foreach (var global in _pdb.EnumerateGlobals())
        {
            if (global.IsPublic)
                continue;
            if (!string.Equals(global.Name, name, StringComparison.Ordinal) &&
                !string.Equals(CleanGlobalName(global.Name), name, StringComparison.Ordinal))
                continue;
            if (!TryGlobalAddress(global, out address))
                continue;
            typeIndex = global.TypeIndex;
            return true;
        }

        return false;
    }

    private bool TryGlobalAddress(GlobalSymbol global, out uint address)
    {
        address = 0;
        var rva = _pdb.Dbi.SectionOffsetToRva(global.Section, (int)global.Offset);
        if (rva == 0 || _moduleBase == 0)
            return false;
        address = _moduleBase + rva;
        return true;
    }

    private static string CleanGlobalName(string name)
    {
        const string anon = "`anonymous namespace'::";
        return name.StartsWith(anon, StringComparison.Ordinal) ? name[anon.Length..] : name;
    }

    private static bool TryReadRegister(string name, ref XContext context, out uint value)
    {
        value = 0;
        var gpr = context.Gpr;
        var n = name.Trim();
        if (n.Length >= 2 && (n[0] is 'r' or 'R') && int.TryParse(n.AsSpan(1), NumberStyles.Integer, CultureInfo.InvariantCulture, out var idx)
            && idx is >= 0 and <= 31 && gpr is { Length: > 0 })
        {
            value = (uint)gpr[idx];
            return true;
        }

        switch (n.ToUpperInvariant())
        {
            case "SP":
                value = gpr is { Length: > 1 } ? (uint)gpr[1] : 0;
                return gpr != null;
            case "IAR":
            case "PC":
                value = context.Iar;
                return true;
            case "LR":
                value = context.Lr;
                return true;
            case "CR":
                value = context.Cr;
                return true;
            case "XER":
                value = context.Xer;
                return true;
            case "MSR":
                value = context.Msr;
                return true;
            default:
                return false;
        }
    }

    private static string AddrRef(uint address, uint typeIndex) => $"@{(ulong)address:x}#{typeIndex}";

    private static bool TryParseAddrRef(string s, out uint address, out uint typeIndex)
    {
        address = 0;
        typeIndex = 0;
        if (s.Length < 2 || s[0] != '@')
            return false;
        var hash = s.IndexOf('#');
        if (hash < 2)
            return false;
        try
        {
            address = (uint)Convert.ToUInt64(s.Substring(1, hash - 1), 16);
            typeIndex = uint.Parse(s.Substring(hash + 1), CultureInfo.InvariantCulture);
            return true;
        }
        catch
        {
            return false;
        }
    }

    private static bool IsStdContainer(string? name, string container) =>
        name is not null && (name.Contains($"::{container}<", StringComparison.Ordinal) ||
                             name.StartsWith($"{container}<", StringComparison.Ordinal));

    private bool TryVectorInfo(PdbType type, uint address, KitMemory memory,
        out uint begin, out uint count, out uint elemTypeIndex, out uint elemSize)
    {
        if (TryNamedPointerPair(type, address, memory, "__begin_", "__end_", out begin, out count, out elemTypeIndex, out elemSize))
            return true;
        return TryNamedPointerPair(type, address, memory, "_Myfirst", "_Mylast", out begin, out count, out elemTypeIndex, out elemSize);
    }

    private bool TryNamedPointerPair(PdbType type, uint address, KitMemory memory,
        string beginName, string endName, out uint begin, out uint count, out uint elemTypeIndex, out uint elemSize)
    {
        begin = count = elemTypeIndex = elemSize = 0;
        type = _pdb.Types.Peel(type.TypeIndex);
        if (!IsStdContainer(type.Name, "vector"))
            return false;
        if (!_pdb.Types.TryFindMember(type.TypeIndex, beginName, out var beginOff, out var beginTypeIdx) ||
            !_pdb.Types.TryFindMember(type.TypeIndex, endName, out var endOff, out _))
            return false;

        var b = memory.ReadDword(address + beginOff);
        var e = memory.ReadDword(address + endOff);
        if (b is null || e is null)
            return false;

        var ptr = _pdb.Types.Peel(beginTypeIdx);
        if (ptr.Kind != PdbTypeKind.Pointer)
            return false;
        elemTypeIndex = ptr.ReferentType != 0 ? ptr.ReferentType : (ptr.TypeIndex & 0xFF);
        if (elemTypeIndex == 0)
            return false;

        elemSize = _pdb.Types.Resolve(elemTypeIndex).ByteSize is var s && s != 0 ? s : 1u;
        begin = b.Value;
        count = e.Value >= b.Value ? (e.Value - b.Value) / elemSize : 0;
        return true;
    }

    private bool TryFormatString(PdbType type, uint address, KitMemory memory, out string value)
    {
        value = string.Empty;
        if (type.Name is null || !type.Name.Contains("basic_string<char,", StringComparison.Ordinal))
            return false;
        // Xbox 360 XDK is MSVC; clang titles use libc++. Probe MSVC members first so the
        // libc++ SSO sniff (low bit of the first dword) cannot steal a VC string.
        if (TryFormatMsvcString(type, address, memory, out value))
            return true;
        return TryFormatLibcxxStringAt(address, memory, out value);
    }

    private bool TryFormatMsvcString(PdbType type, uint address, KitMemory memory, out string value)
    {
        value = string.Empty;
        if (!_pdb.Types.TryFindMember(type.TypeIndex, "_Mysize", out var sizeOff, out _) ||
            !_pdb.Types.TryFindMember(type.TypeIndex, "_Myres", out var resOff, out _))
            return false;
        if (!_pdb.Types.TryFindMember(type.TypeIndex, "_Bx", out var bxOff, out _) &&
            !_pdb.Types.TryFindMember(type.TypeIndex, "_Ptr", out bxOff, out _))
            return false;

        var size = memory.ReadDword(address + sizeOff);
        var res = memory.ReadDword(address + resOff);
        if (size is null || res is null)
            return false;

        uint dataAddr;
        if (res.Value >= 16)
        {
            var ptr = memory.ReadDword(address + bxOff);
            if (ptr is null)
                return false;
            dataAddr = ptr.Value;
        }
        else
        {
            dataAddr = address + bxOff;
        }

        value = ReadCString(dataAddr, 1, memory);
        return true;
    }

    private bool TryFormatLibcxxStringAt(uint address, KitMemory memory, out string value)
    {
        value = string.Empty;
        var b0 = memory.ReadDword(address);
        if (b0 is null)
            return false;
        uint dataAddr;
        if ((b0.Value & 1) != 0)
        {
            var d = memory.ReadDword(address + 8);
            if (d is null)
                return false;
            dataAddr = d.Value;
        }
        else
        {
            dataAddr = address + 1;
        }
        value = ReadCString(dataAddr, 1, memory);
        return true;
    }

    private bool TryContainerSummary(PdbType type, uint address, KitMemory memory, out string summary, out bool expandable)
    {
        summary = string.Empty;
        expandable = false;
        type = _pdb.Types.Peel(type.TypeIndex);
        uint count;
        if (TryVectorInfo(type, address, memory, out _, out count, out _, out _))
        {
            // count set
        }
        else if (IsStdContainer(type.Name, "array") &&
                 _pdb.Types.TryFindMember(type.TypeIndex, "__elems_", out _, out var arrTypeIdx))
        {
            count = _pdb.Types.Resolve(arrTypeIdx).ElementCount;
        }
        else if (IsStdContainer(type.Name, "list") &&
                 (_pdb.Types.TryFindMember(type.TypeIndex, "__size_", out var listSizeOff, out _) ||
                  _pdb.Types.TryFindMember(type.TypeIndex, "_Mysize", out listSizeOff, out _)))
        {
            count = memory.ReadDword(address + listSizeOff) ?? 0;
        }
        else if ((IsStdContainer(type.Name, "set") || IsStdContainer(type.Name, "map")) &&
                 _pdb.Types.TryFindMember(type.TypeIndex, "__tree_", out var treeOff, out var treeType) &&
                 _pdb.Types.TryFindMember(treeType, "__size_", out var treeSizeOff, out _))
        {
            count = memory.ReadDword(address + treeOff + treeSizeOff) ?? 0;
        }
        else
        {
            return false;
        }

        summary = $"{{ size={count} }}";
        expandable = count > 0;
        return true;
    }

    private void EmitElement(uint index, uint typeIndex, uint address, KitMemory memory, ValueList variables) =>
        EmitValue($"[{index}]", typeIndex, address, memory, variables, expandBase: AddrRef(address, typeIndex));

    private bool TryEmitVector(PdbType type, uint address, KitMemory memory, ValueList variables)
    {
        if (!TryVectorInfo(type, address, memory, out var begin, out var count, out var elemTypeIndex, out var elemSize))
            return false;
        var shown = Math.Min(count, 1024u);
        for (uint i = 0; i < shown && !variables.IsFull; i++)
            EmitElement(i, elemTypeIndex, begin + i * elemSize, memory, variables);
        return true;
    }

    private bool TryEmitArray(PdbType type, uint address, KitMemory memory, ValueList variables)
    {
        if (!IsStdContainer(type.Name, "array") ||
            !_pdb.Types.TryFindMember(type.TypeIndex, "__elems_", out var elemsOff, out var elemsTypeIdx))
            return false;
        var arr = _pdb.Types.Resolve(elemsTypeIdx);
        if (arr.Kind != PdbTypeKind.Array || arr.ElementCount == 0)
            return false;
        var elemSize = _pdb.Types.Resolve(arr.ReferentType).ByteSize is var s && s != 0 ? s : 1u;
        var baseAddr = address + elemsOff;
        var count = Math.Min(arr.ElementCount, 1024u);
        for (uint i = 0; i < count && !variables.IsFull; i++)
            EmitElement(i, arr.ReferentType, baseAddr + i * elemSize, memory, variables);
        return true;
    }

    private bool TryNodeValue(uint containerTypeIndex, out uint nodeTypeIndex, out uint valueOffset, out uint valueTypeIndex)
    {
        nodeTypeIndex = 0; valueOffset = 0; valueTypeIndex = 0;
        uint allocType;
        if (!_pdb.Types.TryFindMember(containerTypeIndex, "__node_alloc_", out _, out allocType) &&
            !(_pdb.Types.TryFindMember(containerTypeIndex, "__tree_", out _, out var treeType) &&
              _pdb.Types.TryFindMember(treeType, "__node_alloc_", out _, out allocType)))
            return false;
        var nodeName = ExtractAllocatorArg(_pdb.Types.Resolve(allocType).Name);
        if (nodeName is null || !_pdb.Types.TryFindByName(nodeName, out var nodeType))
            return false;
        nodeTypeIndex = nodeType.TypeIndex;
        return _pdb.Types.TryFindMember(nodeType.TypeIndex, "__value_", out valueOffset, out valueTypeIndex);
    }

    private static string? ExtractAllocatorArg(string? name)
    {
        if (name is null) return null;
        var i = name.IndexOf("allocator<", StringComparison.Ordinal);
        if (i < 0) return null;
        i += "allocator<".Length;
        int depth = 1, start = i;
        for (; i < name.Length && depth > 0; i++)
        {
            if (name[i] == '<') depth++;
            else if (name[i] == '>') depth--;
        }
        return depth == 0 ? name.Substring(start, i - 1 - start).Trim() : null;
    }

    private bool TryEmitList(PdbType type, uint address, KitMemory memory, ValueList variables)
    {
        if (!IsStdContainer(type.Name, "list"))
            return false;
        if (!TryNodeValue(type.TypeIndex, out var nodeTypeIdx, out var valueOff, out var valueTypeIdx) ||
            !_pdb.Types.TryFindMember(type.TypeIndex, "__end_", out var endOff, out _) ||
            !_pdb.Types.TryFindMember(type.TypeIndex, "__size_", out var sizeOff, out _) ||
            !_pdb.Types.TryFindMember(nodeTypeIdx, "__next_", out var nextOff, out _))
            return false;

        var size = memory.ReadDword(address + sizeOff) ?? 0;
        var sentinel = address + endOff;
        var node = memory.ReadDword(sentinel + nextOff);
        for (uint i = 0; node is not null && node.Value != 0 && node.Value != sentinel && i < size && i < 4096 && !variables.IsFull; i++)
        {
            EmitElement(i, valueTypeIdx, node.Value + valueOff, memory, variables);
            node = memory.ReadDword(node.Value + nextOff);
        }
        return true;
    }

    private bool TryEmitTree(PdbType type, uint address, KitMemory memory, ValueList variables)
    {
        if (!IsStdContainer(type.Name, "set") && !IsStdContainer(type.Name, "map"))
            return false;
        if (!_pdb.Types.TryFindMember(type.TypeIndex, "__tree_", out var treeOff, out var treeType) ||
            !TryNodeValue(type.TypeIndex, out var nodeTypeIdx, out var valueOff, out var valueTypeIdx) ||
            !_pdb.Types.TryFindMember(treeType, "__begin_node_", out var beginOff, out _) ||
            !_pdb.Types.TryFindMember(treeType, "__end_node_", out var endNodeOff, out _) ||
            !_pdb.Types.TryFindMember(treeType, "__size_", out var sizeOff, out _) ||
            !_pdb.Types.TryFindMember(nodeTypeIdx, "__left_", out var leftOff, out _) ||
            !_pdb.Types.TryFindMember(nodeTypeIdx, "__right_", out var rightOff, out _) ||
            !_pdb.Types.TryFindMember(nodeTypeIdx, "__parent_", out var parentOff, out _))
            return false;

        var treeAddr = address + treeOff;
        var endNode = treeAddr + endNodeOff;
        var size = memory.ReadDword(treeAddr + sizeOff) ?? 0;
        var node = memory.ReadDword(treeAddr + beginOff);
        for (uint i = 0; node is not null && node.Value != 0 && node.Value != endNode && i < size && i < 4096 && !variables.IsFull; i++)
        {
            EmitElement(i, valueTypeIdx, node.Value + valueOff, memory, variables);
            node = TreeNext(node.Value, endNode, leftOff, rightOff, parentOff, memory);
        }
        return true;
    }

    private static uint? TreeNext(uint node, uint endNode, uint leftOff, uint rightOff, uint parentOff, KitMemory memory)
    {
        var right = memory.ReadDword(node + rightOff) ?? 0;
        if (right != 0)
        {
            var n = right;
            for (var g = 0; g < 4096; g++)
            {
                var l = memory.ReadDword(n + leftOff) ?? 0;
                if (l == 0) break;
                n = l;
            }
            return n;
        }
        for (var g = 0; g < 4096; g++)
        {
            var parent = memory.ReadDword(node + parentOff) ?? 0;
            if (parent == 0 || parent == endNode)
                return endNode;
            if ((memory.ReadDword(parent + leftOff) ?? 0) == node)
                return parent;
            node = parent;
        }
        return endNode;
    }

    private void EmitChildren(uint typeIndex, uint address, KitMemory memory, ValueList variables)
    {
        var type = _pdb.Types.Resolve(typeIndex);

        var peeled = _pdb.Types.Peel(type.TypeIndex);
        if (peeled.Kind == PdbTypeKind.Pointer && peeled.ReferentType != 0)
        {
            var target = memory.ReadDword(address);
            if (target is null || target.Value == 0)
                return;
            EmitChildren(peeled.ReferentType, target.Value, memory, variables);
            return;
        }

        if (TryEmitVector(type, address, memory, variables) ||
            TryEmitArray(type, address, memory, variables) ||
            TryEmitList(type, address, memory, variables) ||
            TryEmitTree(type, address, memory, variables))
            return;

        if (type.Kind == PdbTypeKind.Array && type.ElementCount > 0)
        {
            var elem = _pdb.Types.Resolve(type.ReferentType);
            var elemSize = elem.ByteSize == 0 ? 4u : elem.ByteSize;
            var count = Math.Min(type.ElementCount, 256u);
            for (var i = 0u; i < count && !variables.IsFull; i++)
                EmitElement(i, type.ReferentType, address + i * elemSize, memory, variables);
            return;
        }

        if (type.IsAggregate)
        {
            foreach (var member in type.Members)
            {
                if (variables.IsFull)
                    break;
                if (string.IsNullOrEmpty(member.Name))
                {
                    EmitChildren(member.TypeIndex, (uint)(address + (uint)member.Offset), memory, variables);
                    continue;
                }
                var memberAddr = (uint)(address + (uint)member.Offset);
                EmitValue(member.Name, member.TypeIndex, memberAddr, memory, variables,
                    expandBase: AddrRef(memberAddr, member.TypeIndex));
            }
        }
    }

    private string FormatEnum(PdbType type, uint address, KitMemory memory)
    {
        var size = type.ByteSize == 0 ? 4u : type.ByteSize;
        var raw = memory.ReadSized(address, size);
        if (raw is null)
            return "<unreadable>";
        var v = raw.Value;
        foreach (var e in type.Members)
            if (unchecked((uint)e.Offset) == v)
                return $"{e.Name} ({(int)v})";
        return $"{(int)v} (0x{v:x})";
    }

    private string FormatPointer(PdbType type, uint address, KitMemory memory)
    {
        var ptr = memory.ReadDword(address);
        if (ptr is null)
            return "<unreadable>";
        var value = ptr.Value;
        var width = PointerCharWidth(type);
        if (value != 0 && width != 0)
            return $"0x{value:x8} {ReadCString(value, width, memory)}";
        return $"0x{value:x8}";
    }

    private int PointerCharWidth(PdbType pointerType)
    {
        if (pointerType.ReferentType != 0)
        {
            var referent = _pdb.Types.Peel(pointerType.ReferentType);
            if (referent.Kind == PdbTypeKind.Primitive &&
                referent.Name is "char" or "signed char" or "unsigned char" or "wchar_t")
                return (int)referent.ByteSize;
            return 0;
        }
        return (pointerType.TypeIndex & 0xFF) switch
        {
            0x10 or 0x20 or 0x70 => 1,
            0x71 => 2,
            _ => 0,
        };
    }

    private static string ReadCString(uint address, int charSize, KitMemory memory)
    {
        const int maxChars = 512;
        var sb = new StringBuilder("\"");
        for (var i = 0; i < maxChars; i++)
        {
            var raw = memory.ReadSized(address + (uint)(i * charSize), (uint)charSize);
            if (raw is null) { sb.Append('…'); break; }
            var ch = (int)raw.Value;
            if (ch == 0) break;
            switch (ch)
            {
                case '\n': sb.Append("\\n"); break;
                case '\r': sb.Append("\\r"); break;
                case '\t': sb.Append("\\t"); break;
                default: sb.Append(ch >= 32 ? (char)ch : '.'); break;
            }
            if (i == maxChars - 1) sb.Append('…');
        }
        sb.Append('"');
        return sb.ToString();
    }

    private static string FormatScalar(PdbType type, uint address, KitMemory memory)
    {
        if (type.IsFloatingPoint && type.ByteSize == 8)
        {
            var bits = memory.ReadQword(address);
            if (bits is null)
                return "<unreadable>";
            return $"{BitConverter.Int64BitsToDouble((long)bits.Value):g}";
        }

        if (type.IsFloatingPoint)
        {
            var bits = memory.ReadDword(address);
            if (bits is null)
                return "<unreadable>";
            return $"{BitConverter.Int32BitsToSingle((int)bits.Value):g} (0x{bits.Value:x8})";
        }

        if (type.Kind == PdbTypeKind.Pointer)
        {
            var ptr = memory.ReadDword(address);
            return ptr is null ? "<unreadable>" : $"0x{ptr.Value:x8}";
        }

        if (type.ByteSize == 8)
        {
            var q = memory.ReadQword(address);
            return q is null ? "<unreadable>" : $"0x{q.Value:x16}";
        }

        var size = type.ByteSize == 0 ? 4u : type.ByteSize;
        var raw = memory.ReadSized(address, size);
        if (raw is null)
            return "<unreadable>";
        var value = raw.Value;

        var isUnsigned = type.Name is not null && type.Name.Contains("unsigned", StringComparison.Ordinal);
        return isUnsigned ? $"{value} (0x{value:x8})" : $"{(int)value} (0x{value:x8})";
    }

    private static bool IsHidden(string name) =>
        string.IsNullOrEmpty(name) || name.StartsWith("__", StringComparison.Ordinal);
}
