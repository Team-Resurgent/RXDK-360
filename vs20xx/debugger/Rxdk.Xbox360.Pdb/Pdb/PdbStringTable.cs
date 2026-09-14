// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System.Text;

namespace Rxdk.Xbox360.Pdb.Pdb;

/// <summary>
/// The PDB global string table (the "/names" named stream). CodeView C13 file-checksum records
/// reference source file names by byte offset into this table's string buffer, so line-number
/// resolution needs it to turn a checksum entry into a file path.
/// </summary>
public sealed class PdbStringTable
{
    private const uint ExpectedSignature = 0xEFFEEFFE;

    private readonly byte[] _stringBuffer;

    private PdbStringTable(byte[] stringBuffer) => _stringBuffer = stringBuffer;

    public static PdbStringTable Empty { get; } = new(Array.Empty<byte>());

    /// <summary>
    /// Header is [Signature u32][HashVersion u32][ByteSize u32] followed by the string buffer
    /// (ByteSize bytes of NUL-terminated strings, offset 0 = empty string). Anything after the
    /// buffer (the offset/hash tables) is not needed to resolve a name by offset.
    /// </summary>
    public static PdbStringTable Parse(byte[] stream)
    {
        if (stream.Length < 12)
            return Empty;

        var signature = BitConverter.ToUInt32(stream, 0);
        if (signature != ExpectedSignature)
            return Empty;

        var byteSize = BitConverter.ToInt32(stream, 8);
        if (byteSize < 0 || 12 + byteSize > stream.Length)
            return Empty;

        var buffer = new byte[byteSize];
        Array.Copy(stream, 12, buffer, 0, byteSize);
        return new PdbStringTable(buffer);
    }

    /// <summary>Reads the NUL-terminated string at <paramref name="offset"/>, or "" if out of range.</summary>
    public string GetString(uint offset)
    {
        if (offset >= (uint)_stringBuffer.Length)
            return string.Empty;

        var start = (int)offset;
        var end = start;
        while (end < _stringBuffer.Length && _stringBuffer[end] != 0)
            end++;
        return Encoding.UTF8.GetString(_stringBuffer, start, end - start);
    }
}
