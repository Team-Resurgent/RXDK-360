// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

namespace Rxdk.Xbox360.Pdb.Tpi;

/// <summary>Reads a 4-byte little-endian word from target memory; null if the address is unreadable.</summary>
public delegate uint? MemoryWord(ulong address);

/// <summary>
/// Walks an <see cref="ExpressionPath"/> accessor chain over the TPI type system, starting from a
/// base symbol's address and type, to produce the final value's address and resolved type. Pointer
/// dereferences (for <c>-&gt;</c> and indexing a pointer) read the pointer word from target memory via
/// the supplied <see cref="MemoryWord"/> callback; everything else is pure address arithmetic driven
/// by member offsets and element sizes. This is the toolchain-agnostic replacement for dbghelp's
/// type/expression engine, shared across every platform and unit-tested against the checked-in PDBs.
/// </summary>
public sealed class TypeEvaluator
{
    private readonly TypeSystem _types;
    private readonly MemoryWord _read;

    public TypeEvaluator(TypeSystem types, MemoryWord read)
    {
        _types = types;
        _read = read;
    }

    /// <summary>
    /// Resolves <paramref name="accessors"/> against the value at <paramref name="baseAddress"/> of
    /// type <paramref name="baseType"/>. On success returns the final value's address and resolved
    /// type; on failure returns a short machine-readable <paramref name="error"/> token
    /// (<c>notPointer</c>, <c>memberNotFound</c>, <c>notIndexable</c>, <c>readFailed</c>).
    /// </summary>
    public bool TryWalk(
        ulong baseAddress,
        uint baseType,
        IReadOnlyList<Accessor> accessors,
        out ulong finalAddress,
        out PdbType finalType,
        out string? error)
    {
        error = null;
        var address = baseAddress;
        var type = _types.Resolve(baseType);

        foreach (var accessor in accessors)
        {
            switch (accessor.Kind)
            {
                case AccessorKind.Arrow:
                    if (!TryDereference(ref address, ref type, out var derefReadFailed))
                    {
                        error = derefReadFailed ? "readFailed" : "notPointer";
                        return Fail(out finalAddress, out finalType);
                    }

                    if (!TryMember(ref address, ref type, accessor.Name))
                    {
                        error = "memberNotFound";
                        return Fail(out finalAddress, out finalType);
                    }

                    break;

                case AccessorKind.Member:
                    if (!TryMember(ref address, ref type, accessor.Name))
                    {
                        error = "memberNotFound";
                        return Fail(out finalAddress, out finalType);
                    }

                    break;

                case AccessorKind.Index:
                    if (!TryIndex(ref address, ref type, accessor.Index, out var readFailed))
                    {
                        error = readFailed ? "readFailed" : "notIndexable";
                        return Fail(out finalAddress, out finalType);
                    }

                    break;
            }
        }

        finalAddress = address;
        finalType = type;
        return true;
    }

    private bool TryMember(ref ulong address, ref PdbType type, string name)
    {
        if (!_types.TryFindMember(type.TypeIndex, name, out var offset, out var memberType))
            return false;
        address += offset;
        type = _types.Resolve(memberType);
        return true;
    }

    private bool TryDereference(ref ulong address, ref PdbType type, out bool readFailed)
    {
        readFailed = false;
        var peeled = _types.Peel(type.TypeIndex);
        if (peeled.Kind != PdbTypeKind.Pointer || peeled.ReferentType == 0)
            return false;
        var pointer = _read(address);
        if (pointer is null)
        {
            readFailed = true;
            return false;
        }

        address = pointer.Value;
        type = _types.Resolve(peeled.ReferentType);
        return true;
    }

    private bool TryIndex(ref ulong address, ref PdbType type, long index, out bool readFailed)
    {
        readFailed = false;
        var peeled = _types.Peel(type.TypeIndex);
        if (peeled.ReferentType == 0)
            return false;

        if (peeled.Kind == PdbTypeKind.Array)
        {
            var elem = _types.Resolve(peeled.ReferentType);
            address += (ulong)index * ElemSize(elem);
            type = elem;
            return true;
        }

        if (peeled.Kind == PdbTypeKind.Pointer)
        {
            var pointer = _read(address);
            if (pointer is null)
            {
                readFailed = true;
                return false;
            }

            var elem = _types.Resolve(peeled.ReferentType);
            address = pointer.Value + (ulong)index * ElemSize(elem);
            type = elem;
            return true;
        }

        return false;
    }

    private static uint ElemSize(PdbType elem) => elem.ByteSize == 0 ? 4u : elem.ByteSize;

    private static bool Fail(out ulong finalAddress, out PdbType finalType)
    {
        finalAddress = 0;
        finalType = null!;
        return false;
    }
}
