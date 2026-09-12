// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Rxdk.Xbox360.Dwarf
{
    /// <summary>
    /// Minimal 32-bit big-endian ELF reader - just enough to pull the DWARF debug
    /// sections out of a linked RXDK-360 modern title (.elf). The modern toolchain
    /// links a PowerPC big-endian ELF32, so only that shape is handled.
    /// </summary>
    public sealed class ElfFile
    {
        public readonly byte[] Bytes;
        public bool IsBigEndian { get; private set; }
        public bool Is32Bit { get; private set; }
        public ushort Machine { get; private set; }
        public uint Entry { get; private set; }

        private readonly Dictionary<string, (uint offset, uint size)> _sections =
            new(StringComparer.Ordinal);

        public ElfFile(byte[] bytes)
        {
            Bytes = bytes;
            if (bytes.Length < 52 || bytes[0] != 0x7f || bytes[1] != (byte)'E' ||
                bytes[2] != (byte)'L' || bytes[3] != (byte)'F')
                throw new InvalidDataException("not an ELF file");
            Is32Bit = bytes[4] == 1;
            IsBigEndian = bytes[5] == 2;
            if (!Is32Bit || !IsBigEndian)
                throw new InvalidDataException("expected a 32-bit big-endian ELF (PowerPC)");

            Machine = U16(18);
            Entry = U32(24);
            uint shoff = U32(32);
            ushort shentsize = U16(46);
            ushort shnum = U16(48);
            ushort shstrndx = U16(50);
            if (shoff == 0 || shnum == 0) return;

            // Section-header string table, to resolve section names.
            uint strHdr = shoff + (uint)shstrndx * shentsize;
            uint strOff = U32(strHdr + 16);
            for (int i = 0; i < shnum; i++)
            {
                uint h = shoff + (uint)i * shentsize;
                uint nameIdx = U32(h + 0);
                uint off = U32(h + 16);
                uint size = U32(h + 20);
                string name = CStr(strOff + nameIdx);
                if (name.Length != 0) _sections[name] = (off, size);
            }
        }

        public static ElfFile Load(string path) => new(File.ReadAllBytes(path));

        /// <summary>The bytes of a named section, or an empty array if absent.</summary>
        public byte[] Section(string name)
        {
            if (!_sections.TryGetValue(name, out var s)) return Array.Empty<byte>();
            var outb = new byte[s.size];
            Array.Copy(Bytes, s.offset, outb, 0, (int)s.size);
            return outb;
        }

        public bool HasSection(string name) => _sections.ContainsKey(name);
        public IEnumerable<string> SectionNames => _sections.Keys;

        private ushort U16(uint o) => (ushort)((Bytes[o] << 8) | Bytes[o + 1]);
        private uint U32(uint o) =>
            (uint)((Bytes[o] << 24) | (Bytes[o + 1] << 16) | (Bytes[o + 2] << 8) | Bytes[o + 3]);
        private string CStr(uint o)
        {
            int end = (int)o;
            while (end < Bytes.Length && Bytes[end] != 0) end++;
            return Encoding.UTF8.GetString(Bytes, (int)o, end - (int)o);
        }
    }
}
