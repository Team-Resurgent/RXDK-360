// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// HRESULT values from the PC-side Xbox 360 Debug Monitor header
// (%XEDK%\include\win32\xbdm.h). FACILITY_XBDM is 0x2da (port 730).

namespace Rxdk.Xbox360.Xbdm
{
    public static class XbdmHResults
    {
        public const int Facility = 0x2da;

        public static int Success(int code) => (Facility << 16) | code;
        public static int Error(int code) => unchecked((int)(0x80000000 | (uint)Success(code)));

        public static bool IsSuccess(int hr) => hr >= 0;

        public const int NoErr = 0x02DA0000;
        public const int Connected = 0x02DA0001;
        public const int MultiResponse = 0x02DA0002;
        public const int BinResponse = 0x02DA0003;
        public const int ReadyForBin = 0x02DA0004;
        public const int Dedicated = 0x02DA0005;

        public const int NoSuchFile = unchecked((int)0x82DA0002);
        public const int MemUnmapped = unchecked((int)0x82DA0004);
        public const int NoThread = unchecked((int)0x82DA0005);
        public const int NotStopped = unchecked((int)0x82DA0008);
        public const int AlreadyExists = unchecked((int)0x82DA000A);
        public const int CannotAccess = unchecked((int)0x82DA000E);
        public const int NotDebuggable = unchecked((int)0x82DA0010);
        public const int CannotConnect = unchecked((int)0x82DA0100);
        public const int ConnectionLost = unchecked((int)0x82DA0101);
        public const int EndOfList = unchecked((int)0x82DA0104);
        public const int BufferTooSmall = unchecked((int)0x82DA0105);
        public const int NoXboxName = unchecked((int)0x82DA0108);
    }
}
