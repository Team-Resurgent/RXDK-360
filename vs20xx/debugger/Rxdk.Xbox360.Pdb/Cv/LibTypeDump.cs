// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System.Text;
using Rxdk.Xbox360.Pdb.Internal;
using Rxdk.Xbox360.Pdb.Tpi;

namespace Rxdk.Xbox360.Pdb.Cv;

/// <summary>
/// Recovers ground-truth C type layouts (structs/unions/enums with byte offsets, sizes, and enum
/// values) from the CodeView <c>.debug$T</c> sections carried inside a COFF static library (.lib).
/// This lets us read the exact ABI a prebuilt library was compiled against — e.g. reconciling the
/// RXDK libs (built from the Jan-2002 source leak) to the XDK-5849 prebuilt libraries — without
/// disassembling a single instruction. Every object member owns an independent type stream indexed
/// from 0x1000; named definitions are merged across members and de-duplicated.
/// </summary>
public static class LibTypeDump
{
    private const uint ImageFileMachineI386 = 0x014C;

    /// <summary>A recovered aggregate/enum, keyed by name for cross-member de-duplication.</summary>
    public sealed record RecoveredType(string Name, PdbTypeKind Kind, uint ByteSize, string Rendered, int Weight);

    /// <summary>
    /// Extracts every named struct/union/enum from <paramref name="libPath"/>. If
    /// <paramref name="nameFilters"/> is non-empty, only types whose name contains one of the
    /// (case-insensitive) substrings are returned. Results are ordered by name.
    /// </summary>
    public static IReadOnlyList<RecoveredType> Extract(string libPath, IReadOnlyList<string>? nameFilters = null)
    {
        var lib = File.ReadAllBytes(libPath);
        var best = new Dictionary<string, RecoveredType>(StringComparer.Ordinal);

        foreach (var member in EnumerateObjectMembers(lib))
        {
            var debugT = FindSection(member, ".debug$T");
            if (debugT.IsEmpty || debugT.Length <= 4)
                continue;

            // Skip the 4-byte CV signature (CV_SIGNATURE_C13 == 4) that precedes the records.
            var records = debugT.Slice(4).ToArray();
            TpiStream tpi;
            try
            {
                tpi = TpiStream.FromTypeRecords(records);
            }
            catch
            {
                continue;
            }

            var types = new TypeSystem(tpi);
            foreach (var index in tpi.TypeIndices())
            {
                RenderIfNamed(tpi, types, index, best);
            }
        }

        IEnumerable<RecoveredType> result = best.Values;
        if (nameFilters is { Count: > 0 })
        {
            result = result.Where(t => nameFilters.Any(f =>
                t.Name.Contains(f, StringComparison.OrdinalIgnoreCase)));
        }

        return result.OrderBy(t => t.Name, StringComparer.Ordinal).ToList();
    }

    /// <summary>Convenience: the concatenated C-like rendering of every matching type.</summary>
    public static string Render(string libPath, IReadOnlyList<string>? nameFilters = null)
    {
        var sb = new StringBuilder();
        foreach (var t in Extract(libPath, nameFilters))
        {
            sb.Append(t.Rendered);
            sb.Append('\n');
        }
        return sb.ToString();
    }

    private static void RenderIfNamed(TpiStream tpi, TypeSystem types, uint index,
        Dictionary<string, RecoveredType> best)
    {
        var t = types.Resolve(index);
        if (string.IsNullOrEmpty(t.Name))
            return;

        string rendered;
        int weight;
        switch (t.Kind)
        {
            case PdbTypeKind.Struct:
            case PdbTypeKind.Class:
            case PdbTypeKind.Union:
                if (t.Members.Count == 0)
                    return; // forward ref / empty; a fuller def elsewhere wins
                rendered = RenderAggregate(types, t);
                weight = t.Members.Count;
                break;
            case PdbTypeKind.Enum:
                var enumerators = ReadEnumerators(tpi, index);
                if (enumerators.Count == 0)
                    return;
                rendered = RenderEnum(t, enumerators);
                weight = enumerators.Count;
                break;
            default:
                return;
        }

        // Prefer the richest definition seen for a given name.
        if (!best.TryGetValue(t.Name, out var existing) || weight > existing.Weight)
            best[t.Name] = new RecoveredType(t.Name, t.Kind, t.ByteSize, rendered, weight);
    }

    private static string RenderAggregate(TypeSystem types, PdbType t)
    {
        var keyword = t.Kind == PdbTypeKind.Union ? "union" : "struct";
        var sb = new StringBuilder();
        sb.Append($"{keyword} {t.Name} {{  // sizeof = {t.ByteSize} (0x{t.ByteSize:X})\n");
        foreach (var m in t.Members)
        {
            var mt = types.Resolve(m.TypeIndex);
            var typeName = mt.Name ?? $"t#{m.TypeIndex:X}";
            var size = mt.ByteSize;
            sb.Append($"    /* +{m.Offset,-4} (0x{m.Offset:X2})  sz {size,-4} */ {typeName} {m.Name};\n");
        }
        sb.Append("};\n");
        return sb.ToString();
    }

    private static string RenderEnum(PdbType t, IReadOnlyList<(string Name, long Value)> enumerators)
    {
        var sb = new StringBuilder();
        sb.Append($"enum {t.Name} {{  // sizeof = {t.ByteSize}\n");
        foreach (var (name, value) in enumerators)
            sb.Append($"    {name} = {value},\n");
        sb.Append("};\n");
        return sb.ToString();
    }

    /// <summary>Reads an enum record's field list and collects its LF_ENUMERATE name/value pairs.</summary>
    private static List<(string Name, long Value)> ReadEnumerators(TpiStream tpi, uint enumIndex)
    {
        var list = new List<(string, long)>();
        var rec = new LeReader(tpi.GetRecord(enumIndex));
        if ((TypeLeaf)rec.ReadUInt16() != TypeLeaf.Enum)
            return list;
        _ = rec.ReadUInt16();          // count
        _ = rec.ReadUInt16();          // property
        _ = rec.ReadUInt32();          // underlying type
        var fieldList = rec.ReadUInt32();

        var visited = new HashSet<uint>();
        WalkEnumFieldList(tpi, fieldList, list, visited);
        return list;
    }

    private static void WalkEnumFieldList(TpiStream tpi, uint fieldList,
        List<(string, long)> into, HashSet<uint> visited)
    {
        if (fieldList == 0 || !tpi.IsRecordIndex(fieldList) || !visited.Add(fieldList))
            return;

        var r = new LeReader(tpi.GetRecord(fieldList));
        if ((TypeLeaf)r.ReadUInt16() != TypeLeaf.FieldList)
            return;

        while (r.Remaining >= 2)
        {
            SkipPadding(r);
            if (r.Remaining < 2)
                break;
            var sub = (TypeLeaf)r.ReadUInt16();
            switch (sub)
            {
                case TypeLeaf.Enumerate:
                    _ = r.ReadUInt16();               // attr
                    var value = r.ReadNumericLeaf();
                    var name = r.ReadCString();
                    into.Add((name, value));
                    break;
                case TypeLeaf.Index:
                    _ = r.ReadUInt16();
                    var next = r.ReadUInt32();
                    WalkEnumFieldList(tpi, next, into, visited);
                    break;
                default:
                    return;
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
            r.Position += b & 0x0F;
        }
    }

    // ---- COFF archive (.lib) + object parsing ----------------------------------------------------

    /// <summary>Yields the raw bytes of each COFF object member (skips linker/longname/import members).</summary>
    private static IEnumerable<byte[]> EnumerateObjectMembers(byte[] lib)
    {
        // "!<arch>\n"
        if (lib.Length < 8 || Encoding.ASCII.GetString(lib, 0, 8) != "!<arch>\n")
            yield break;

        var pos = 8;
        while (pos + 60 <= lib.Length)
        {
            var sizeStr = Encoding.ASCII.GetString(lib, pos + 48, 10).Trim();
            if (!int.TryParse(sizeStr, out var size) || size <= 0)
                yield break;

            var dataStart = pos + 60;
            if (dataStart + size > lib.Length)
                yield break;

            var member = new byte[size];
            Array.Copy(lib, dataStart, member, 0, size);

            // A COFF object begins with the i386 machine word; linker/longname members and
            // import-headers (Sig1=0, Sig2=0xFFFF) don't.
            if (size >= 20 && ReadU16(member, 0) == ImageFileMachineI386)
                yield return member;

            pos = dataStart + size + (size & 1); // members are 2-byte aligned
        }
    }

    /// <summary>Returns the raw data of the named section in a COFF object, or empty.</summary>
    private static ReadOnlySpan<byte> FindSection(byte[] obj, string name)
    {
        var numSections = ReadU16(obj, 2);
        var optHeaderSize = ReadU16(obj, 16);
        var secTableStart = 20 + optHeaderSize;

        for (var i = 0; i < numSections; i++)
        {
            var sh = secTableStart + i * 40;
            if (sh + 40 > obj.Length)
                break;

            var secName = Encoding.ASCII.GetString(obj, sh, 8).TrimEnd('\0');
            if (secName != name)
                continue;

            var sizeOfRawData = (int)ReadU32(obj, sh + 16);
            var ptrToRawData = (int)ReadU32(obj, sh + 20);
            if (ptrToRawData <= 0 || sizeOfRawData <= 0 || ptrToRawData + sizeOfRawData > obj.Length)
                return ReadOnlySpan<byte>.Empty;

            return obj.AsSpan(ptrToRawData, sizeOfRawData);
        }

        return ReadOnlySpan<byte>.Empty;
    }

    private static ushort ReadU16(byte[] b, int o) => (ushort)(b[o] | (b[o + 1] << 8));
    private static uint ReadU32(byte[] b, int o) =>
        (uint)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24));
}
