// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// CLI over the XBDM client, to exercise the devkit debug path:
//
//   RxdkXbdm info                              locate/load xbdm.dll, show context size (offline)
//   RxdkXbdm threads <console>                 connect and list thread ids + PC
//   RxdkXbdm deploy  <console> <file> <remote> send a file to the console
//   RxdkXbdm launch  <console> <imagePath>     reboot the console into a title (stopped)
//   RxdkXbdm mem     <console> <hexAddr> <len> read memory
//
// Everything but "info" needs a real devkit reachable on the network.

using System;
using System.Globalization;
using System.Runtime.InteropServices;
using Rxdk.Xbox360.Xbdm;

if (args.Length < 1) { Console.Error.WriteLine("usage: RxdkXbdm <info|threads|deploy|launch|mem> ..."); return 1; }

try
{
    switch (args[0])
    {
        case "info":
        {
            // Offline check: the resolver can find + load xbdm.dll, and the context
            // struct marshals to a sane size.
            int size = Marshal.SizeOf<XContext>();
            Console.WriteLine($"XContext marshalled size: {size} bytes (expected 2624)");
            string? dll = XbdmClient.LocateXbdm();
            Console.WriteLine(dll != null ? $"xbdm.dll found: {dll}" : "xbdm.dll NOT found (install the XDK / set XEDK).");
            break;
        }
        case "threads":
        {
            using var c = new XbdmClient();
            c.Connect(args[1]);
            c.AttachDebugger();
            foreach (var tid in c.GetThreads())
            {
                var ctx = c.GetContext(tid, XContextFlags.Control | XContextFlags.Integer);
                Console.WriteLine($"  thread 0x{tid:X8}  PC=0x{ctx.Iar:X8}  LR=0x{ctx.Lr:X8}  r1=0x{ctx.Gpr[1]:X16}");
            }
            break;
        }
        case "deploy":
        {
            using var c = new XbdmClient();
            c.Connect(args[1]);
            c.SendFile(args[2], args[3]);
            Console.WriteLine($"sent {args[2]} -> {args[3]}");
            break;
        }
        case "launch":
        {
            using var c = new XbdmClient();
            c.Connect(args[1]);
            c.AttachDebugger();
            c.LaunchTitle(args[2], stopAtLoad: true);
            Console.WriteLine($"launched {args[2]} (stopped at load)");
            break;
        }
        case "mem":
        {
            using var c = new XbdmClient();
            c.Connect(args[1]);
            uint addr = ParseHex(args[2]);
            int len = int.Parse(args[3]);
            var bytes = c.ReadMemory(addr, len);
            Console.WriteLine($"0x{addr:X8}: " + BitConverter.ToString(bytes));
            break;
        }
        default:
            Console.Error.WriteLine("unknown command: " + args[0]);
            return 1;
    }
}
catch (Exception e) { Console.Error.WriteLine("error: " + e.Message); return 1; }
return 0;

static uint ParseHex(string s)
{
    s = s.Trim(); if (s.StartsWith("0x") || s.StartsWith("0X")) s = s[2..];
    return uint.Parse(s, NumberStyles.HexNumber);
}
