// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

namespace Rxdk.Xbox360.Xbdm
{
    /// <summary>
    /// Convert between the link EXE (PDB / DWARF VMAs) and the live XEX on the kit.
    /// Both share an RVA; only the bases differ (and usually both are 0x82000000).
    ///
    /// XBDM capture breakpoints are already XEX VAs — <see cref="XexToRva"/> to find
    /// the PDB entry, <see cref="ExeToXex"/> / <see cref="RvaToXex"/> to plant.
    /// </summary>
    public static class TitleAddress
    {
        public const uint DefaultBase = 0x82000000;

        public static uint XexToRva(uint xexVa, uint xexBase = DefaultBase) =>
            xexVa >= xexBase ? xexVa - xexBase : xexVa;

        public static uint RvaToXex(uint rva, uint xexBase = DefaultBase) =>
            xexBase + rva;

        public static uint ExeToRva(uint exeVa, uint exeBase = DefaultBase) =>
            exeVa >= exeBase ? exeVa - exeBase : exeVa;

        public static uint RvaToExe(uint rva, uint exeBase = DefaultBase) =>
            exeBase + rva;

        /// <summary>PDB / ELF VMA → execute breakpoint on the loaded XEX.</summary>
        public static uint ExeToXex(uint exeVa, uint xexBase, uint exeBase = DefaultBase) =>
            RvaToXex(ExeToRva(exeVa, exeBase), xexBase);

        /// <summary>Live XEX VA (already-adjusted capture BP) → EXE / PDB VMA.</summary>
        public static uint XexToExe(uint xexVa, uint xexBase, uint exeBase = DefaultBase) =>
            RvaToExe(XexToRva(xexVa, xexBase), exeBase);
    }
}
