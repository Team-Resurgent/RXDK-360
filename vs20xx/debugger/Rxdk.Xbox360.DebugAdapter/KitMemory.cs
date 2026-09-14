// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Buffers.Binary;
using Rxdk.Xbox360.Xbdm;

namespace Rxdk.Xbox360.DebugAdapter;

/// <summary>
/// Big-endian reads from the stopped title. Xbox 360 memory is PowerPC BE; a little-endian
/// dword (the original-Xbox DAP path) would swap every scalar and every pointer.
/// </summary>
internal sealed class KitMemory
{
    private readonly XbdmClient _kit;

    public KitMemory(XbdmClient kit) => _kit = kit;

    public byte[]? ReadBytes(uint address, int length)
    {
        if (length <= 0)
            return Array.Empty<byte>();
        try
        {
            return _kit.ReadMemory(address, length);
        }
        catch
        {
            return null;
        }
    }

    public uint? ReadDword(uint address)
    {
        var b = ReadBytes(address, 4);
        if (b is null || b.Length < 4)
            return null;
        return BinaryPrimitives.ReadUInt32BigEndian(b);
    }

    public ulong? ReadQword(uint address)
    {
        var b = ReadBytes(address, 8);
        if (b is null || b.Length < 8)
            return null;
        return BinaryPrimitives.ReadUInt64BigEndian(b);
    }

    public uint? ReadSized(uint address, uint byteSize) => byteSize switch
    {
        1 => ReadBytes(address, 1) is { Length: >= 1 } b1 ? b1[0] : null,
        2 => ReadBytes(address, 2) is { Length: >= 2 } b2 ? BinaryPrimitives.ReadUInt16BigEndian(b2) : null,
        8 => ReadQword(address) is ulong q ? (uint)q : null, // callers that need 64 bits use ReadQword
        _ => ReadDword(address),
    };
}
