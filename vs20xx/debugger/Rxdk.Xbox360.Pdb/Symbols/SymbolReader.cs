// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using Rxdk.Xbox360.Pdb.Dbi;
using Rxdk.Xbox360.Pdb.Internal;
using Rxdk.Xbox360.Pdb.Msf;

namespace Rxdk.Xbox360.Pdb.Symbols;

/// <summary>
/// Reads per-module CodeView symbol streams to answer "what are the locals of the function at this
/// RVA?". Handles the modern S_LOCAL + S_DEFRANGE_FRAMEPOINTER_REL encoding (Zig/LLVM) as well as
/// the legacy S_BPREL32 / S_REGREL32 records, so the same reader works across toolchains.
/// </summary>
public sealed class SymbolReader
{
    private const int CodeViewSignatureC13 = 4;
    private const ushort CvRegEsp = 21; // x86 ESP
    private const ushort CvRegEbp = 22; // x86 EBP
    private const ushort CvAllRegVFrame = 0x7536; // CV_ALLREG_VFRAME (the realigned virtual frame base)
    private const ushort CvPpcGpr0 = 1;  // CV_PPC_GPR0
    private const ushort CvPpcGpr31 = 32; // CV_PPC_GPR31
    private const ushort LocalFlagIsParameter = 0x0001;
    private const ushort LocalFlagCompilerGenerated = 0x0004;

    private readonly MsfFile _msf;
    private readonly DbiStream _dbi;

    public SymbolReader(MsfFile msf, DbiStream dbi)
    {
        _msf = msf;
        _dbi = dbi;
    }

    /// <summary>Finds the function whose code covers <paramref name="rva"/> and returns its locals.</summary>
    public FrameInfo? FindFrame(uint rva)
    {
        // Prefer the module the section contributions attribute this RVA to; fall back to scanning
        // all symbol-bearing modules (contribution data can be sparse/absent).
        var mapped = _dbi.FindModuleByRva(rva);
        if (mapped is { HasSymbols: true })
        {
            var hit = FindInModule(mapped, rva);
            if (hit is not null)
                return hit;
        }

        foreach (var module in _dbi.Modules)
        {
            if (!module.HasSymbols || ReferenceEquals(module, mapped))
                continue;
            var hit = FindInModule(module, rva);
            if (hit is not null)
                return hit;
        }

        return null;
    }

    /// <summary>Enumerates every function (with its frame-relative locals) across all modules.</summary>
    public IEnumerable<FrameInfo> EnumerateFunctions()
    {
        foreach (var module in _dbi.Modules)
        {
            if (!module.HasSymbols)
                continue;
            foreach (var frame in EnumerateModuleFrames(module))
                yield return frame;
        }
    }

    /// <summary>
    /// Enumerates the program's global-scope data symbols by walking the flat symbol-record stream
    /// (the blob the GSI/PSI hash tables index into). Yields S_GDATA32 globals and S_LDATA32 statics
    /// with their type index, plus S_PUB32 publics (no type). Unlike the per-module symbol streams,
    /// this stream has no leading CodeView signature, so parsing starts at offset 0.
    /// </summary>
    public IEnumerable<GlobalSymbol> EnumerateGlobals()
    {
        var streamIndex = _dbi.SymbolRecordStreamIndex;
        if (streamIndex < 0 || streamIndex >= _msf.StreamCount)
            yield break;

        var stream = _msf.ReadStream(streamIndex);
        var r = new LeReader(stream);

        while (r.Remaining >= 4)
        {
            var recLen = r.ReadUInt16();
            var recEnd = r.Position + recLen;
            if (recLen < 2 || recEnd > r.Length)
                break;
            var kind = (SymbolKind)r.ReadUInt16();

            switch (kind)
            {
                case SymbolKind.GData32:
                case SymbolKind.LData32:
                {
                    var type = r.ReadUInt32();
                    var offset = r.ReadUInt32();
                    var segment = r.ReadUInt16();
                    var name = r.ReadCString();
                    yield return new GlobalSymbol(name, type, segment, offset, IsPublic: false);
                    break;
                }

                case SymbolKind.Pub32:
                {
                    _ = r.ReadUInt32(); // pubsymflags
                    var offset = r.ReadUInt32();
                    var segment = r.ReadUInt16();
                    var name = r.ReadCString();
                    yield return new GlobalSymbol(name, 0, segment, offset, IsPublic: true);
                    break;
                }
            }

            r.Position = recEnd;
        }
    }

    private FrameInfo? FindInModule(DbiModule module, uint rva)
    {
        foreach (var frame in EnumerateModuleFrames(module))
        {
            if (rva >= frame.FunctionRva && rva < frame.FunctionRva + frame.CodeSize)
                return frame;
        }

        return null;
    }

    private IEnumerable<FrameInfo> EnumerateModuleFrames(DbiModule module)
    {
        var stream = _msf.ReadStream(module.SymbolStreamIndex);
        if (stream.Length < 4)
            yield break;

        var r = new LeReader(stream) { Position = CodeViewSignatureC13 }; // skip the CV signature (C13)

        var depth = 0;
        var inlineDepth = 0; // locals inside inline sites belong to the inlined callee, not this frame
        string funcName = string.Empty;
        uint funcRva = 0;
        uint codeSize = 0;
        List<LocalVariable> locals = new();
        string? pendingName = null;
        uint pendingType = 0;
        bool pendingIsParam = false;
        uint calleeSavedBytes = 0;
        var localBase = FrameBase.Ebp;
        var paramBase = FrameBase.Ebp;

        while (r.Remaining >= 4)
        {
            var recLen = r.ReadUInt16();
            var recEnd = r.Position + recLen;
            if (recEnd > r.Length)
                break;
            var kind = (SymbolKind)r.ReadUInt16();

            switch (kind)
            {
                case SymbolKind.GProc32:
                case SymbolKind.LProc32:
                case SymbolKind.GProc32Id:
                case SymbolKind.LProc32Id:
                {
                    var proc = ReadProc(r);
                    if (depth == 0)
                    {
                        depth = 1;
                        funcName = proc.Name;
                        funcRva = _dbi.SectionOffsetToRva(proc.Segment, (int)proc.CodeOffset);
                        codeSize = proc.CodeSize;
                        locals = new List<LocalVariable>();
                        pendingName = null;
                        inlineDepth = 0;
                        calleeSavedBytes = 0;
                        localBase = FrameBase.Ebp;
                        paramBase = FrameBase.Ebp;
                    }
                    else
                    {
                        depth++; // nested proc scope
                    }

                    break;
                }

                case SymbolKind.Block32:
                    if (depth > 0)
                        depth++;
                    break;

                case SymbolKind.FrameProc when depth == 1:
                {
                    // FrameProcSym: totalFrame, padding, offPad, cbCalleeSaved, offExHdlr(u32),
                    // secExHdlr(u16), flags(u32). We need cbCalleeSaved (to rebuild VFRAME from
                    // EBP) and the encoded local/param base-pointer registers from flags.
                    _ = r.ReadUInt32();                 // total frame bytes
                    _ = r.ReadUInt32();                 // padding bytes
                    _ = r.ReadUInt32();                 // offset to padding
                    calleeSavedBytes = r.ReadUInt32();  // bytes of callee-saved registers
                    _ = r.ReadUInt32();                 // exception-handler offset
                    _ = r.ReadUInt16();                 // exception-handler section
                    var frameFlags = r.ReadUInt32();
                    // bits 14-15 = encoded local base ptr, 16-17 = encoded param base ptr.
                    // x86 encoding: 1=ESP, 2=EBP, 3=VFRAME (the realigned virtual frame base).
                    localBase = DecodeFrameBase((frameFlags >> 14) & 0x3);
                    paramBase = DecodeFrameBase((frameFlags >> 16) & 0x3);
                    break;
                }

                case SymbolKind.InlineSite:
                    if (depth > 0)
                    {
                        depth++;
                        inlineDepth++;
                    }

                    break;

                case SymbolKind.End:
                case SymbolKind.ProcIdEnd:
                case SymbolKind.InlineSiteEnd:
                    if (depth > 0)
                    {
                        if (kind == SymbolKind.InlineSiteEnd && inlineDepth > 0)
                            inlineDepth--;
                        if (--depth == 0 && funcRva != 0)
                        {
                            yield return new FrameInfo
                            {
                                FunctionName = funcName,
                                FunctionRva = funcRva,
                                CodeSize = codeSize,
                                Locals = locals,
                                CalleeSavedBytes = calleeSavedBytes,
                            };
                        }
                    }

                    break;

                case SymbolKind.Local when depth > 0 && inlineDepth == 0:
                {
                    var type = r.ReadUInt32();
                    var flags = r.ReadUInt16();
                    var name = r.ReadCString();
                    if ((flags & LocalFlagCompilerGenerated) != 0)
                    {
                        pendingName = null;
                    }
                    else
                    {
                        pendingName = name;
                        pendingType = type;
                        pendingIsParam = (flags & LocalFlagIsParameter) != 0;
                    }

                    break;
                }

                case SymbolKind.DefRangeFramePointerRel when depth > 0 && inlineDepth == 0 && pendingName is not null:
                case SymbolKind.DefRangeFramePointerRelFullScope when depth > 0 && inlineDepth == 0 && pendingName is not null:
                {
                    var offset = r.ReadInt32(); // offset from the frame's local/param base register
                    var baseReg = pendingIsParam ? paramBase : localBase;
                    locals.Add(new LocalVariable(pendingName, pendingType, offset, pendingIsParam, baseReg));
                    pendingName = null; // first range fixes the location
                    break;
                }

                case SymbolKind.DefRangeRegisterRel when depth > 0 && inlineDepth == 0 && pendingName is not null:
                {
                    // DefRangeRegisterRel: baseReg(u16), flags(u16), basePointerOffset(i32), range...
                    var reg = r.ReadUInt16();
                    _ = r.ReadUInt16(); // flags (spilledUdtMember / offsetParent) -- unused
                    var offset = r.ReadInt32();
                    if (TryMapRegister(reg, out var baseReg, out var gpr))
                        locals.Add(new LocalVariable(pendingName, pendingType, offset, pendingIsParam, baseReg, gpr));
                    pendingName = null;
                    break;
                }

                case SymbolKind.BpRel32 when depth > 0 && inlineDepth == 0:
                {
                    var offset = r.ReadInt32();
                    var type = r.ReadUInt32();
                    var name = r.ReadCString();
                    // Xbox 360 MSVC "BP" is r1 after the stwu prologue.
                    locals.Add(new LocalVariable(name, type, offset, false, FrameBase.Gpr, 1));
                    break;
                }

                case SymbolKind.RegRel32 when depth > 0 && inlineDepth == 0:
                {
                    var offset = r.ReadInt32();
                    var type = r.ReadUInt32();
                    var reg = r.ReadUInt16();
                    var name = r.ReadCString();
                    if (TryMapRegister(reg, out var baseReg, out var gpr))
                        locals.Add(new LocalVariable(name, type, offset, false, baseReg, gpr));
                    break;
                }
            }

            r.Position = recEnd; // skip trailing bytes (def-range gaps, unparsed fields)
        }
    }

    private static ProcRecord ReadProc(LeReader r)
    {
        _ = r.ReadUInt32(); // parent
        _ = r.ReadUInt32(); // end
        _ = r.ReadUInt32(); // next
        var codeSize = r.ReadUInt32();
        _ = r.ReadUInt32(); // debug start
        _ = r.ReadUInt32(); // debug end
        _ = r.ReadUInt32(); // function type
        var codeOffset = r.ReadUInt32();
        var segment = r.ReadUInt16();
        _ = r.ReadByte();   // flags
        var name = r.ReadCString();
        return new ProcRecord(name, codeSize, codeOffset, segment);
    }

    // S_FRAMEPROC encoded base-pointer register (x86): 1=ESP, 2=EBP, 3=VFRAME; 0=none (default EBP).
    private static FrameBase DecodeFrameBase(uint encoded) => encoded switch
    {
        1 => FrameBase.Esp,
        2 => FrameBase.Ebp,
        3 => FrameBase.VFrame,
        _ => FrameBase.Ebp,
    };

    /// <summary>
    /// Maps a CodeView register id to a frame base. Xbox 360 MSVC emits CV_PPC_GPR0..31
    /// (1..32) on S_REGREL32; clang CodeView still names x86 EBP/ESP/VFRAME.
    /// </summary>
    private static bool TryMapRegister(ushort reg, out FrameBase frameBase, out int gpr)
    {
        gpr = 0;
        switch (reg)
        {
            case CvRegEsp:
                frameBase = FrameBase.Esp;
                return true;
            case CvRegEbp:
                frameBase = FrameBase.Ebp;
                return true;
            case CvAllRegVFrame:
                frameBase = FrameBase.VFrame;
                return true;
        }

        if (reg is >= CvPpcGpr0 and <= CvPpcGpr31)
        {
            frameBase = FrameBase.Gpr;
            gpr = reg - CvPpcGpr0;
            return true;
        }

        frameBase = FrameBase.Ebp;
        return false;
    }

    private readonly record struct ProcRecord(string Name, uint CodeSize, uint CodeOffset, ushort Segment);
}
