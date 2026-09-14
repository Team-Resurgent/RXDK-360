// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

namespace Rxdk.Xbox360.Pdb.Symbols;

/// <summary>CodeView symbol record kinds (S_*) used for procedure/local/def-range parsing.</summary>
internal enum SymbolKind : ushort
{
    End = 0x0006,
    FrameProc = 0x1012,
    Block32 = 0x1103,
    BpRel32 = 0x110B,
    LData32 = 0x110C,
    GData32 = 0x110D,
    Pub32 = 0x110E,
    LProc32 = 0x110F,
    GProc32 = 0x1110,
    RegRel32 = 0x1111,
    Local = 0x113E,
    DefRangeFramePointerRel = 0x1142,
    DefRangeRegisterRel = 0x1145,
    DefRangeFramePointerRelFullScope = 0x1144,
    LProc32Id = 0x1146,
    GProc32Id = 0x1147,
    InlineSite = 0x114D,
    InlineSiteEnd = 0x114E,
    ProcIdEnd = 0x114F,
}
