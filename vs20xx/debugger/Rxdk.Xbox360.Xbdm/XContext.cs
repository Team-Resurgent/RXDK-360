// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System.Runtime.InteropServices;

namespace Rxdk.Xbox360.Xbdm
{
    /// <summary>
    /// The Xenon (PowerPC) thread context, matching the XDK's XCONTEXT (xectx.h) so
    /// DmGetThreadContext / DmSetThreadContext can marshal it directly. GPRs and the
    /// count register are 64-bit; the instruction address (Iar) is the program
    /// counter a source-level debugger maps through DWARF.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct XContext
    {
        public uint ContextFlags;

        // CONTROL
        public uint Msr;
        public uint Iar;       // instruction address register = PC
        public uint Lr;        // link register
        public ulong Ctr;      // count register

        // INTEGER
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public ulong[] Gpr;   // r0..r31
        public uint Cr;        // condition register
        public uint Xer;       // fixed-point exception register

        // FLOATING_POINT
        public double Fpscr;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public double[] Fpr;   // f0..f31
        public uint UserModeControl;
        public uint Fill;

        // VECTOR: Vscr[4] then Vr0..Vr127 each float[4] = 4 + 512 floats.
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 516)] public float[] Vector;

        public static XContext Create(uint flags = XContextFlags.Full) => new()
        {
            ContextFlags = flags,
            Gpr = new ulong[32],
            Fpr = new double[32],
            Vector = new float[516],
        };
    }

    public static class XContextFlags
    {
        public const uint Control = 0x00000001;
        public const uint FloatingPoint = 0x00000002;
        public const uint Integer = 0x00000004;
        public const uint Vector = 0x00000010;
        public const uint Full = Control | FloatingPoint | Integer | Vector;
    }
}
