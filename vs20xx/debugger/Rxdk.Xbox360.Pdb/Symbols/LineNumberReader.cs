// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using Rxdk.Xbox360.Pdb.Dbi;
using Rxdk.Xbox360.Pdb.Internal;
using Rxdk.Xbox360.Pdb.Msf;
using Rxdk.Xbox360.Pdb.Pdb;

namespace Rxdk.Xbox360.Pdb.Symbols;

/// <summary>One contiguous run of code mapped to a single source line.</summary>
public sealed record LineEntry(uint RvaStart, uint RvaEnd, uint Line, string File);

/// <summary>
/// Parses the CodeView "C13" line-number info (DEBUG_S_LINES + DEBUG_S_FILECHECKSUMS subsections)
/// that follows the symbols in each module's MSF stream, producing an image-RVA ↔ (file, line)
/// map. This is the managed replacement for dbghelp's SymGetLineFromAddr64 / SymGetLineFromName64,
/// letting source-level debugging work off a PDB on non-Windows hosts.
///
/// "C13" is the modern CodeView debug-info format version (CV_SIGNATURE_C13 = 4) — it has nothing
/// to do with the C11/C23 language standards; clang/Zig emit it for C++23 titles all the same.
/// </summary>
public sealed class LineNumberReader
{
    private const uint SubsectionLines = 0xF2;          // DEBUG_S_LINES
    private const uint SubsectionFileChecksums = 0xF4;  // DEBUG_S_FILECHECKSUMS
    private const ushort LineFlagHaveColumns = 0x0001;  // CV_LINES_HAVE_COLUMNS
    private const uint LineNumberMask = 0x00FFFFFF;      // low 24 bits = start line
    private const uint LineNumberSentinelFloor = 0x00F00000; // 0xFEEFEE etc. == "no line"

    private readonly LineEntry[] _byRva; // sorted by RvaStart

    public LineNumberReader(MsfFile msf, DbiStream dbi, PdbStringTable names)
    {
        var entries = new List<LineEntry>();
        foreach (var module in dbi.Modules)
        {
            if (!module.HasLineInfo)
                continue;
            try
            {
                ParseModule(msf, dbi, names, module, entries);
            }
            catch
            {
                // A single malformed module must not sink the whole line table.
            }
        }

        entries.Sort((a, b) => a.RvaStart.CompareTo(b.RvaStart));
        _byRva = entries.ToArray();
    }

    public IReadOnlyList<LineEntry> Entries => _byRva;

    /// <summary>Maps an image RVA to its source file and line (the run that covers it).</summary>
    public bool TryFindLine(uint rva, out string file, out uint line)
    {
        file = string.Empty;
        line = 0;

        // Greatest RvaStart <= rva.
        int lo = 0, hi = _byRva.Length - 1, idx = -1;
        while (lo <= hi)
        {
            var mid = (lo + hi) / 2;
            if (_byRva[mid].RvaStart <= rva)
            {
                idx = mid;
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }

        if (idx < 0)
            return false;

        var entry = _byRva[idx];
        if (rva >= entry.RvaEnd)
            return false; // in a gap beyond the covering run

        file = entry.File;
        line = entry.Line;
        return true;
    }

    /// <summary>
    /// Maps a source file + line to an image RVA. Prefers an exact line match (lowest address);
    /// otherwise slides forward to the next line that has code in the same file, matching how a
    /// debugger places a breakpoint on the next executable statement.
    /// </summary>
    public bool TryResolveLine(string file, uint line, out uint rva)
    {
        rva = 0;
        var wanted = FileBase(file);

        var exactFound = false;
        uint exactRva = 0;
        var nearestFound = false;
        uint nearestLine = 0;
        uint nearestRva = 0;

        foreach (var entry in _byRva)
        {
            if (!FileMatches(wanted, entry.File))
                continue;

            if (entry.Line == line)
            {
                if (!exactFound || entry.RvaStart < exactRva)
                {
                    exactRva = entry.RvaStart;
                    exactFound = true;
                }
            }
            else if (entry.Line > line)
            {
                if (!nearestFound || entry.Line < nearestLine ||
                    (entry.Line == nearestLine && entry.RvaStart < nearestRva))
                {
                    nearestLine = entry.Line;
                    nearestRva = entry.RvaStart;
                    nearestFound = true;
                }
            }
        }

        if (exactFound)
        {
            rva = exactRva;
            return true;
        }

        if (nearestFound)
        {
            rva = nearestRva;
            return true;
        }

        return false;
    }

    private static void ParseModule(MsfFile msf, DbiStream dbi, PdbStringTable names, DbiModule module, List<LineEntry> entries)
    {
        var stream = msf.ReadStream(module.SymbolStreamIndex);
        var start = module.C13LineInfoOffset;
        var end = start + (int)module.C13ByteSize;
        if (start < 0 || end > stream.Length)
            return;

        // fileId (byte offset into the checksums subsection) -> source file name.
        var fileNames = new Dictionary<uint, string>();
        var lineBlocks = new List<(int Offset, int Length)>();

        // Checksums may appear either before or after the line subsections, so collect the line
        // subsections and resolve their file ids only after the whole region has been scanned.
        var r = new LeReader(stream) { Position = start };
        while (r.Position + 8 <= end)
        {
            var kind = r.ReadUInt32();
            var length = (int)r.ReadUInt32();
            var dataStart = r.Position;
            if (length < 0 || dataStart + length > end)
                break;

            if (kind == SubsectionFileChecksums)
                ParseFileChecksums(stream, dataStart, length, names, fileNames);
            else if (kind == SubsectionLines)
                lineBlocks.Add((dataStart, length));

            r.Position = dataStart + length;
            r.Align(4);
        }

        foreach (var (offset, length) in lineBlocks)
            ParseLines(stream, dbi, offset, length, fileNames, entries);
    }

    private static void ParseFileChecksums(byte[] stream, int start, int length, PdbStringTable names, Dictionary<uint, string> fileNames)
    {
        var r = new LeReader(stream) { Position = start };
        var end = start + length;
        while (r.Position + 6 <= end)
        {
            var fileId = (uint)(r.Position - start); // byte offset of this entry = the id lines use
            var nameOffset = r.ReadUInt32();
            var checksumSize = r.ReadByte();
            _ = r.ReadByte(); // checksum kind
            r.Position += checksumSize;
            r.Align(4);
            fileNames[fileId] = names.GetString(nameOffset);
        }
    }

    private static void ParseLines(byte[] stream, DbiStream dbi, int start, int length, Dictionary<uint, string> fileNames, List<LineEntry> entries)
    {
        var r = new LeReader(stream) { Position = start };
        var end = start + length;

        var codeStart = r.ReadUInt32();      // section-relative offset of the covered code
        var segment = r.ReadUInt16();
        var flags = r.ReadUInt16();
        var codeSize = r.ReadUInt32();
        var hasColumns = (flags & LineFlagHaveColumns) != 0;

        while (r.Position + 12 <= end)
        {
            var fileId = r.ReadUInt32();
            var numLines = r.ReadUInt32();
            _ = r.ReadUInt32();              // block size (incl. header) -- entries follow directly

            if (numLines == 0 || (long)numLines * 8 > end - r.Position)
                break; // implausible count -- bail rather than run off the subsection

            var file = fileNames.TryGetValue(fileId, out var name) ? name : string.Empty;

            var offsets = new uint[numLines];
            var lines = new uint[numLines];
            for (uint i = 0; i < numLines; i++)
            {
                offsets[i] = r.ReadUInt32();
                lines[i] = r.ReadUInt32() & LineNumberMask;
            }

            if (hasColumns)
                r.Position += (int)numLines * 4; // 2x u16 per line -- not needed

            for (uint i = 0; i < numLines; i++)
            {
                if (lines[i] == 0 || lines[i] >= LineNumberSentinelFloor)
                    continue;

                var cvStart = codeStart + offsets[i];
                var cvEnd = i + 1 < numLines ? codeStart + offsets[i + 1] : codeStart + codeSize;
                var rvaStart = dbi.SectionOffsetToRva(segment, (int)cvStart);
                if (rvaStart == 0)
                    continue;
                var rvaEnd = dbi.SectionOffsetToRva(segment, (int)cvEnd);
                if (rvaEnd <= rvaStart)
                    rvaEnd = rvaStart + 1;

                entries.Add(new LineEntry(rvaStart, rvaEnd, lines[i], file));
            }
        }
    }

    private static string FileBase(string path)
    {
        var normalized = path.Replace('\\', '/');
        var slash = normalized.LastIndexOf('/');
        return slash >= 0 ? normalized[(slash + 1)..] : normalized;
    }

    private static bool FileMatches(string wantedBase, string candidate)
    {
        if (string.IsNullOrEmpty(candidate))
            return false;
        return string.Equals(FileBase(candidate), wantedBase, StringComparison.OrdinalIgnoreCase);
    }
}
