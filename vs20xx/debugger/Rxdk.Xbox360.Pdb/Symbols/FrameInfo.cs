// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

namespace Rxdk.Xbox360.Pdb.Symbols;

/// <summary>
/// The frame register a local's <see cref="LocalVariable.FrameOffset"/> is measured from.
/// Xbox 360 MSVC PDBs use <see cref="Gpr"/> (almost always r1 / CV_PPC_GPR1) via S_REGREL32.
/// x86 leftovers remain for clang CodeView that still names EBP/ESP/VFRAME.
/// </summary>
public enum FrameBase
{
    Ebp,
    Esp,
    VFrame,
    /// <summary>A PowerPC GPR; <see cref="LocalVariable.Register"/> is the GPR index (0–31).</summary>
    Gpr,
}

/// <summary>
/// A local variable located relative to a frame register (<see cref="Base"/>).
/// <see cref="FrameOffset"/> is added to that register's value to get the variable's address;
/// <see cref="TypeIndex"/> resolves against the TPI type system for size/shape.
/// When <see cref="Base"/> is <see cref="FrameBase.Gpr"/>, <see cref="Register"/> is r0–r31.
/// </summary>
public sealed record LocalVariable(
    string Name, uint TypeIndex, long FrameOffset, bool IsParameter,
    FrameBase Base = FrameBase.Ebp, int Register = 0);

/// <summary>
/// The function whose code contains a queried RVA, plus its frame-relative locals. This is the
/// managed replacement for dbghelp's (broken) locals enumeration on Zig/LLVM PDBs.
/// </summary>
public sealed class FrameInfo
{
    public required string FunctionName { get; init; }
    public required uint FunctionRva { get; init; }
    public required uint CodeSize { get; init; }
    public required IReadOnlyList<LocalVariable> Locals { get; init; }

    /// <summary>
    /// Bytes of callee-saved registers pushed after the frame pointer (from S_FRAMEPROC). Needed
    /// to reconstruct <see cref="FrameBase.VFrame"/> from EBP: VFRAME = (EBP - this) &amp; ~0xF.
    /// </summary>
    public uint CalleeSavedBytes { get; init; }
}
