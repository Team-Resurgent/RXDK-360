// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

namespace Rxdk.Xbox360.Pdb.Tpi;

/// <summary>
/// CodeView type-record leaf kinds (the ones this reader understands). Values match the
/// microsoft-pdb / cvinfo.h LF_* constants.
/// </summary>
internal enum TypeLeaf : ushort
{
    Modifier = 0x1001,
    Pointer = 0x1002,
    Procedure = 0x1008,
    MFunction = 0x1009,
    ArgList = 0x1201,
    FieldList = 0x1203,
    BitField = 0x1205,
    Index = 0x1404,   // continuation to another FieldList
    Enumerate = 0x1502,
    Array = 0x1503,
    Class = 0x1504,
    Structure = 0x1505,
    Union = 0x1506,
    Enum = 0x1507,
    Member = 0x150D,
    StaticMember = 0x150E,
    Method = 0x150F,
    NestType = 0x1510,
    VFuncTab = 0x1409,
    OneMethod = 0x1511,
    BClass = 0x1400,
    VBClass = 0x1401,
    IVBClass = 0x1402,
}
