// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using Rxdk.Xbox360.Pdb.Dbi;
using Rxdk.Xbox360.Pdb.Msf;
using Rxdk.Xbox360.Pdb.Pdb;
using Rxdk.Xbox360.Pdb.Symbols;
using Rxdk.Xbox360.Pdb.Tpi;

namespace Rxdk.Xbox360.Pdb;

/// <summary>
/// Top-level entry point for reading a PDB. Opens the MSF container and exposes the well-known
/// streams. Higher layers (TPI types, DBI modules, per-module symbols) build on this.
/// </summary>
public sealed class PdbImage
{
    private readonly MsfFile _msf;
    private PdbInfoStream? _info;
    private TpiStream? _tpi;
    private TypeSystem? _types;
    private DbiStream? _dbi;
    private SymbolReader? _symbols;
    private PdbStringTable? _names;
    private LineNumberReader? _lines;

    private PdbImage(MsfFile msf) => _msf = msf;

    public static PdbImage Open(byte[] image) => new(MsfFile.Open(image));

    public static PdbImage OpenFile(string path) => Open(File.ReadAllBytes(path));

    /// <summary>The underlying MSF container (stream access, block size, stream count).</summary>
    public MsfFile Msf => _msf;

    /// <summary>PDB Information stream (version, signature, age, GUID, named streams).</summary>
    public PdbInfoStream Info => _info ??= PdbInfoStream.Parse(_msf.ReadStream(PdbInfoStream.StreamIndex));

    /// <summary>TPI stream (CodeView type records).</summary>
    public TpiStream Tpi => _tpi ??= TpiStream.Parse(_msf.ReadStream(TpiStream.StreamIndex));

    /// <summary>Type resolver over the TPI stream (sizes, names, pointer/array shape, members).</summary>
    public TypeSystem Types => _types ??= new TypeSystem(Tpi);

    /// <summary>DBI stream (modules, section contributions, section headers; RVA→module lookup).</summary>
    public DbiStream Dbi => _dbi ??= DbiStream.Parse(_msf);

    /// <summary>Per-module symbol reader (procedures + frame-relative locals).</summary>
    public SymbolReader Symbols => _symbols ??= new SymbolReader(_msf, Dbi);

    /// <summary>Finds the function containing an image RVA and returns its frame-relative locals.</summary>
    public FrameInfo? FindFrame(uint rva) => Symbols.FindFrame(rva);

    /// <summary>Enumerates the program's global-scope data symbols (globals, statics, publics).</summary>
    public IEnumerable<GlobalSymbol> EnumerateGlobals() => Symbols.EnumerateGlobals();

    /// <summary>The "/names" global string table (source file names for line info).</summary>
    public PdbStringTable Names => _names ??= LoadNames();

    /// <summary>Image-RVA ↔ (file, line) mapping parsed from the C13 line info.</summary>
    public LineNumberReader Lines => _lines ??= new LineNumberReader(_msf, Dbi, Names);

    /// <summary>Maps an image RVA to its source file and line.</summary>
    public bool TryFindLine(uint rva, out string file, out uint line) => Lines.TryFindLine(rva, out file, out line);

    /// <summary>Maps a source file + line to an image RVA (exact, else the next line with code).</summary>
    public bool TryResolveLine(string file, uint line, out uint rva) => Lines.TryResolveLine(file, line, out rva);

    /// <summary>Name of the function whose code contains an image RVA, or false if none.</summary>
    public bool TryFindFunctionName(uint rva, out string name)
    {
        var frame = FindFrame(rva);
        if (frame is not null && !string.IsNullOrEmpty(frame.FunctionName))
        {
            name = frame.FunctionName;
            return true;
        }

        name = string.Empty;
        return false;
    }

    /// <summary>Image RVA of a function or data symbol by name (leading-underscore insensitive).</summary>
    public bool TryFindSymbolRva(string name, out uint rva)
    {
        rva = 0;
        if (string.IsNullOrWhiteSpace(name))
            return false;

        foreach (var fn in Symbols.EnumerateFunctions())
        {
            if (NameMatches(fn.FunctionName, name))
            {
                rva = fn.FunctionRva;
                return true;
            }
        }

        foreach (var g in EnumerateGlobals())
        {
            if (!NameMatches(g.Name, name))
                continue;
            var resolved = Dbi.SectionOffsetToRva(g.Section, (int)g.Offset);
            if (resolved != 0)
            {
                rva = resolved;
                return true;
            }
        }

        return false;
    }

    private PdbStringTable LoadNames()
    {
        if (Info.NamedStreams.TryGetValue("/names", out var index) &&
            index >= 0 && index < _msf.StreamCount)
        {
            return PdbStringTable.Parse(_msf.ReadStream(index));
        }

        return PdbStringTable.Empty;
    }

    // dbghelp-style lookups pass C names with a leading underscore ("_main"); PDB symbol records may
    // carry it or not depending on toolchain, so match with and without.
    private static bool NameMatches(string symbol, string wanted) =>
        string.Equals(symbol, wanted, StringComparison.Ordinal) ||
        string.Equals(symbol.TrimStart('_'), wanted.TrimStart('_'), StringComparison.Ordinal);
}
