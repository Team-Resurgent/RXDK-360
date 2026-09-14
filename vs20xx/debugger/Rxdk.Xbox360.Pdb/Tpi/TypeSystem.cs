// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using Rxdk.Xbox360.Pdb.Internal;

namespace Rxdk.Xbox360.Pdb.Tpi;

/// <summary>
/// Resolves CodeView type indices into <see cref="PdbType"/> values: sizes, names, pointer/array
/// shape, and struct members. Primitive indices are resolved by formula; record indices are parsed
/// from the TPI stream. Results are cached and forward references are redirected to their real
/// definition so a local's type resolves to a concrete size.
/// </summary>
public sealed class TypeSystem
{
    private const ushort PropForwardRef = 0x80;

    private readonly TpiStream _tpi;
    private readonly Dictionary<uint, PdbType> _cache = new();
    private readonly HashSet<uint> _resolving = new();
    private Dictionary<string, uint>? _definitions; // name -> non-fwdref record index

    public TypeSystem(TpiStream tpi) => _tpi = tpi;

    public PdbType Resolve(uint typeIndex)
    {
        if (_cache.TryGetValue(typeIndex, out var cached))
            return cached;

        var result = ResolveUncached(typeIndex);
        _cache[typeIndex] = result;
        return result;
    }

    /// <summary>Convenience: byte size of a type index (0 if it cannot be determined).</summary>
    public uint SizeOf(uint typeIndex) => Resolve(typeIndex).ByteSize;

    /// <summary>
    /// Follows <see cref="PdbTypeKind.Modifier"/> wrappers (const/volatile) to the underlying type so
    /// callers see the real struct/array/pointer kind rather than the modifier shell.
    /// </summary>
    public PdbType Peel(uint typeIndex)
    {
        var type = Resolve(typeIndex);
        for (var guard = 0; type.Kind == PdbTypeKind.Modifier && type.ReferentType != 0 && guard < 8; guard++)
            type = Resolve(type.ReferentType);
        return type;
    }

    /// <summary>
    /// Finds a data member by name within an aggregate, returning its byte offset (accumulated from
    /// the outer type) and type index. Modifiers are peeled, an exact match is preferred over a
    /// case-insensitive one, and anonymous (unnamed) member aggregates are searched recursively so a
    /// field inside an anonymous union/struct resolves by its own name. Returns false for non-aggregates
    /// or an unknown member.
    /// </summary>
    public bool TryFindMember(uint typeIndex, string name, out uint memberOffset, out uint memberType) =>
        TryFindMemberCore(typeIndex, name, baseOffset: 0, out memberOffset, out memberType, depth: 0);

    private bool TryFindMemberCore(uint typeIndex, string name, uint baseOffset, out uint memberOffset, out uint memberType, int depth)
    {
        memberOffset = 0;
        memberType = 0;
        if (depth > 8 || string.IsNullOrEmpty(name))
            return false;

        var type = Peel(typeIndex);
        if (!type.IsAggregate)
            return false;

        foreach (var member in type.Members)
        {
            if (string.Equals(member.Name, name, StringComparison.Ordinal))
            {
                memberOffset = baseOffset + (uint)member.Offset;
                memberType = member.TypeIndex;
                return true;
            }
        }

        foreach (var member in type.Members)
        {
            if (string.Equals(member.Name, name, StringComparison.OrdinalIgnoreCase))
            {
                memberOffset = baseOffset + (uint)member.Offset;
                memberType = member.TypeIndex;
                return true;
            }
        }

        foreach (var member in type.Members)
        {
            if (string.IsNullOrEmpty(member.Name) &&
                TryFindMemberCore(member.TypeIndex, name, baseOffset + (uint)member.Offset, out memberOffset, out memberType, depth + 1))
                return true;
        }

        return false;
    }

    /// <summary>Looks up a struct/class/union/enum by name (its real, non-forward-ref definition).</summary>
    public bool TryFindByName(string name, out PdbType type)
    {
        _definitions ??= BuildDefinitionMap();
        if (!string.IsNullOrEmpty(name) && _definitions.TryGetValue(name, out var index))
        {
            type = Resolve(index);
            return true;
        }

        type = Unknown(0);
        return false;
    }

    private PdbType ResolveUncached(uint typeIndex)
    {
        if (!_tpi.IsRecordIndex(typeIndex))
        {
            if (PrimitiveTypes.TryResolve(typeIndex, out var prim))
            {
                return new PdbType
                {
                    TypeIndex = typeIndex,
                    Kind = (typeIndex >> 8 & 0x7) != 0 ? PdbTypeKind.Pointer : PdbTypeKind.Primitive,
                    ByteSize = prim.ByteSize,
                    Name = prim.Name,
                    IsFloatingPoint = prim.IsFloat,
                };
            }

            return Unknown(typeIndex);
        }

        if (!_resolving.Add(typeIndex))
            return Unknown(typeIndex); // cycle guard

        try
        {
            return ParseRecord(typeIndex);
        }
        finally
        {
            _resolving.Remove(typeIndex);
        }
    }

    private PdbType ParseRecord(uint typeIndex)
    {
        var r = new LeReader(_tpi.GetRecord(typeIndex).ToArray());
        var leaf = (TypeLeaf)r.ReadUInt16();

        switch (leaf)
        {
            case TypeLeaf.Modifier:
            {
                var underlying = r.ReadUInt32();
                var inner = Resolve(underlying);
                return new PdbType
                {
                    TypeIndex = typeIndex,
                    Kind = PdbTypeKind.Modifier,
                    ByteSize = inner.ByteSize,
                    Name = inner.Name,
                    IsFloatingPoint = inner.IsFloatingPoint,
                    ReferentType = underlying,
                    ElementCount = inner.ElementCount,
                    Members = inner.Members,
                };
            }

            case TypeLeaf.Pointer:
            {
                var utype = r.ReadUInt32();
                var attr = r.ReadUInt32();
                var size = (attr >> 13) & 0x3F; // pointer size in bytes, 0 => default
                var referentName = SafeName(utype);
                return new PdbType
                {
                    TypeIndex = typeIndex,
                    Kind = PdbTypeKind.Pointer,
                    ByteSize = size == 0 ? 4 : size,
                    Name = referentName is null ? null : referentName + "*",
                    ReferentType = utype,
                };
            }

            case TypeLeaf.Array:
            {
                var elemType = r.ReadUInt32();
                _ = r.ReadUInt32(); // index type
                var totalBytes = (uint)r.ReadNumericLeaf();
                var name = r.ReadCString();
                var elemSize = SafeSize(elemType);
                var count = elemSize == 0 ? 0u : totalBytes / elemSize;
                return new PdbType
                {
                    TypeIndex = typeIndex,
                    Kind = PdbTypeKind.Array,
                    ByteSize = totalBytes,
                    Name = string.IsNullOrEmpty(name) ? null : name,
                    ReferentType = elemType,
                    ElementCount = count,
                };
            }

            case TypeLeaf.Class:
            case TypeLeaf.Structure:
            {
                _ = r.ReadUInt16(); // member count
                var property = r.ReadUInt16();
                var fieldList = r.ReadUInt32();
                _ = r.ReadUInt32(); // derived
                _ = r.ReadUInt32(); // vshape
                var size = (uint)r.ReadNumericLeaf();
                var name = r.ReadCString();
                return ResolveAggregate(typeIndex, leaf == TypeLeaf.Class ? PdbTypeKind.Class : PdbTypeKind.Struct,
                    property, fieldList, size, name);
            }

            case TypeLeaf.Union:
            {
                _ = r.ReadUInt16(); // member count
                var property = r.ReadUInt16();
                var fieldList = r.ReadUInt32();
                var size = (uint)r.ReadNumericLeaf();
                var name = r.ReadCString();
                return ResolveAggregate(typeIndex, PdbTypeKind.Union, property, fieldList, size, name);
            }

            case TypeLeaf.Enum:
            {
                _ = r.ReadUInt16(); // count
                var property = r.ReadUInt16();
                var utype = r.ReadUInt32();
                var fieldList = r.ReadUInt32();
                var name = r.ReadCString();
                if ((property & PropForwardRef) != 0 && TryRedirect(name, out var def))
                    return Resolve(def);
                var inner = Resolve(utype);
                // Capture the enumerators (name + value, value stored in PdbMember.Offset) so a value
                // can be shown by name; enums aren't aggregates, so these are never emitted as children.
                var enumerators = new List<PdbMember>();
                if (fieldList != 0 && _tpi.IsRecordIndex(fieldList))
                    ParseFieldList(fieldList, enumerators, new HashSet<uint>());
                return new PdbType
                {
                    TypeIndex = typeIndex,
                    Kind = PdbTypeKind.Enum,
                    ByteSize = inner.ByteSize == 0 ? 4 : inner.ByteSize,
                    Name = string.IsNullOrEmpty(name) ? null : name,
                    ReferentType = utype,
                    Members = enumerators,
                };
            }

            case TypeLeaf.Procedure:
            case TypeLeaf.MFunction:
                return new PdbType { TypeIndex = typeIndex, Kind = PdbTypeKind.Procedure, ByteSize = 0 };

            default:
                return Unknown(typeIndex);
        }
    }

    private PdbType ResolveAggregate(uint typeIndex, PdbTypeKind kind, ushort property, uint fieldList, uint size, string name)
    {
        // Forward references carry no members and often size 0; redirect to the real definition.
        if ((property & PropForwardRef) != 0 && TryRedirect(name, out var def) && def != typeIndex)
            return Resolve(def);

        var members = new List<PdbMember>();
        if (fieldList != 0 && _tpi.IsRecordIndex(fieldList))
            ParseFieldList(fieldList, members, new HashSet<uint>());

        return new PdbType
        {
            TypeIndex = typeIndex,
            Kind = kind,
            ByteSize = size,
            Name = string.IsNullOrEmpty(name) ? null : name,
            Members = members,
        };
    }

    private void ParseFieldList(uint fieldListIndex, List<PdbMember> into, HashSet<uint> visited)
    {
        if (!visited.Add(fieldListIndex) || !_tpi.IsRecordIndex(fieldListIndex))
            return;

        var r = new LeReader(_tpi.GetRecord(fieldListIndex).ToArray());
        if ((TypeLeaf)r.ReadUInt16() != TypeLeaf.FieldList)
            return;

        while (r.Remaining >= 2)
        {
            SkipPadding(r);
            if (r.Remaining < 2)
                break;

            var subLeaf = (TypeLeaf)r.ReadUInt16();
            switch (subLeaf)
            {
                case TypeLeaf.Member:
                {
                    _ = r.ReadUInt16();          // attr
                    var type = r.ReadUInt32();
                    var offset = r.ReadNumericLeaf();
                    var name = r.ReadCString();
                    into.Add(new PdbMember(name, offset, type));
                    break;
                }

                case TypeLeaf.Index: // continuation to another field list
                {
                    _ = r.ReadUInt16();          // pad
                    var next = r.ReadUInt32();
                    ParseFieldList(next, into, visited);
                    break;
                }

                case TypeLeaf.StaticMember:
                    _ = r.ReadUInt16();
                    _ = r.ReadUInt32();
                    _ = r.ReadCString();
                    break;

                case TypeLeaf.VFuncTab:
                    _ = r.ReadUInt16();
                    _ = r.ReadUInt32();
                    break;

                case TypeLeaf.NestType:
                    _ = r.ReadUInt16();
                    _ = r.ReadUInt32();
                    _ = r.ReadCString();
                    break;

                case TypeLeaf.Method:
                    _ = r.ReadUInt16();          // count
                    _ = r.ReadUInt32();          // method list
                    _ = r.ReadCString();
                    break;

                case TypeLeaf.OneMethod:
                {
                    var attr = r.ReadUInt16();
                    _ = r.ReadUInt32();          // type
                    var mprop = (attr >> 2) & 0x7;
                    if (mprop is 4 or 6)         // intro / pure intro virtual carries a vtable offset
                        _ = r.ReadUInt32();
                    _ = r.ReadCString();
                    break;
                }

                case TypeLeaf.BClass:
                {
                    // A (non-virtual) base class. Record it as a nameless member at its offset so
                    // member lookup recurses into it (TryFindMemberCore treats empty-name members
                    // as base classes) -- e.g. an inherited m_pd3dDevice resolves through `this`.
                    _ = r.ReadUInt16();          // attr
                    var baseType = r.ReadUInt32();
                    var baseOffset = r.ReadNumericLeaf();
                    into.Add(new PdbMember(string.Empty, baseOffset, baseType));
                    break;
                }

                case TypeLeaf.VBClass:
                case TypeLeaf.IVBClass:
                    _ = r.ReadUInt16();          // attr
                    _ = r.ReadUInt32();          // base type
                    _ = r.ReadUInt32();          // virtual base pointer type
                    _ = r.ReadNumericLeaf();     // vbptr offset
                    _ = r.ReadNumericLeaf();     // vbase offset
                    break;

                case TypeLeaf.Enumerate:
                {
                    _ = r.ReadUInt16();          // attr
                    var evalue = r.ReadNumericLeaf(); // enumerator value
                    var ename = r.ReadCString();
                    into.Add(new PdbMember(ename, evalue, 0)); // value carried in Offset
                    break;
                }

                default:
                    return; // unknown sub-record: stop rather than misparse
            }
        }
    }

    private static void SkipPadding(LeReader r)
    {
        while (r.Remaining >= 1)
        {
            var b = r.Span[r.Position];
            if (b < 0xF0)
                break;
            r.Position += b & 0x0F; // LF_PAD: low nibble = bytes to skip (incl. this one)
        }
    }

    private bool TryRedirect(string name, out uint definitionIndex)
    {
        definitionIndex = 0;
        if (string.IsNullOrEmpty(name))
            return false;
        _definitions ??= BuildDefinitionMap();
        return _definitions.TryGetValue(name, out definitionIndex);
    }

    private Dictionary<string, uint> BuildDefinitionMap()
    {
        var map = new Dictionary<string, uint>(StringComparer.Ordinal);
        foreach (var index in _tpi.TypeIndices())
        {
            var record = _tpi.GetRecord(index);
            if (record.Length < 2)
                continue;
            var r = new LeReader(record);
            var leaf = (TypeLeaf)r.ReadUInt16();
            string? name = null;
            switch (leaf)
            {
                case TypeLeaf.Class:
                case TypeLeaf.Structure:
                {
                    _ = r.ReadUInt16();
                    var property = r.ReadUInt16();
                    if ((property & PropForwardRef) != 0)
                        continue;
                    r.ReadUInt32(); r.ReadUInt32(); r.ReadUInt32();
                    _ = r.ReadNumericLeaf();
                    name = r.ReadCString();
                    break;
                }
                case TypeLeaf.Union:
                {
                    _ = r.ReadUInt16();
                    var property = r.ReadUInt16();
                    if ((property & PropForwardRef) != 0)
                        continue;
                    r.ReadUInt32();
                    _ = r.ReadNumericLeaf();
                    name = r.ReadCString();
                    break;
                }
                case TypeLeaf.Enum:
                {
                    _ = r.ReadUInt16();
                    var property = r.ReadUInt16();
                    if ((property & PropForwardRef) != 0)
                        continue;
                    r.ReadUInt32(); r.ReadUInt32();
                    name = r.ReadCString();
                    break;
                }
            }

            if (!string.IsNullOrEmpty(name))
                map.TryAdd(name, index);
        }

        return map;
    }

    private string? SafeName(uint typeIndex)
    {
        if (_resolving.Contains(typeIndex))
            return null; // avoid recursion just to build a pointer name
        return Resolve(typeIndex).Name;
    }

    private uint SafeSize(uint typeIndex)
    {
        if (_resolving.Contains(typeIndex))
            return 0;
        return Resolve(typeIndex).ByteSize;
    }

    private static PdbType Unknown(uint typeIndex) =>
        new() { TypeIndex = typeIndex, Kind = PdbTypeKind.Unknown, ByteSize = 0 };
}
