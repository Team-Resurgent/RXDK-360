// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// The RXDK-360 devkit debug adapter. Normally launched by the editor and spoken to
// over stdio in the Debug Adapter Protocol:
//
//   RxdkDebugAdapter                     run the DAP server on stdio (editor use)
//   RxdkDebugAdapter --selftest <elf>    offline check of the symbol glue (no devkit)

using System;
using System.Collections.Generic;
using Rxdk.Xbox360.DebugAdapter;
using Rxdk.Xbox360.Dwarf;
using Rxdk.Xbox360.Xbdm;

if (args.Length >= 2 && args[0] == "--selftest")
    return SelfTest(args[1]);

var dap = new DapConnection(Console.OpenStandardInput(), Console.OpenStandardOutput());
var session = new DebugSession(dap);
while (!session.Terminated)
{
    var msg = dap.Read();
    if (msg == null) break;
    if (msg.Type == "request") session.Handle(msg);
}
return 0;

// Exercise the symbol side end to end without a console: resolve a breakpoint,
// map an address back to source, and lay out a function's locals against a
// synthetic register context.
static int SelfTest(string elf)
{
    var sym = Symbols.FromElf(elf);
    int fns = 0; foreach (var _ in sym.Info.Functions) fns++;
    Console.WriteLine($"symbols: {fns} function(s)");

    foreach (var fn in sym.Info.Functions)
    {
        Console.WriteLine($"\n{fn.Name}  0x{fn.LowPc:X8}-0x{fn.HighPc:X8}  ({System.IO.Path.GetFileName(fn.File)}:{fn.DeclLine})");

        var bp = sym.ResolveBreakpoint(System.IO.Path.GetFileName(fn.File), fn.DeclLine + 1);
        if (bp != null) Console.WriteLine($"  breakpoint {System.IO.Path.GetFileName(fn.File)}:{fn.DeclLine + 1} -> 0x{bp.Value.address:X8} (line {bp.Value.line})");

        var ln = sym.LineAt(fn.LowPc);
        Console.WriteLine($"  entry 0x{fn.LowPc:X8} -> {(ln != null ? $"{System.IO.Path.GetFileName(ln.File)}:{ln.Line}" : "<none>")}");

        // Synthetic frame: seed every GPR with a base so whichever register the
        // compiler chose as the frame base yields a visible frameBase + offset.
        var ctx = XContext.Create();
        for (int i = 0; i < 32; i++) ctx.Gpr[i] = 0x70000000;
        foreach (var s in sym.Locals(fn, ctx))
        {
            string where = s.Address is uint a ? $"@0x{a:X8}" : s.Register is int r ? $"r{r}" : "<none>";
            Console.WriteLine($"    {(s.TypeName + " " + s.Name).PadRight(24)} {where}  (size {s.Size})");
        }
    }
    return 0;
}
