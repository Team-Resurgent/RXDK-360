// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// CLI over the DWARF reader, for validating symbol extraction from a modern
// title's .elf without a devkit:
//
//   RxdkDwarf <title.elf>                 list functions
//   RxdkDwarf <title.elf> --lines         dump the line table
//   RxdkDwarf <title.elf> --locals        list functions with their locals
//   RxdkDwarf <title.elf> --addr 0x82..   resolve an address to function+source
//   RxdkDwarf <title.elf> --line main.c:2 resolve a source line to an address

using System;
using System.Globalization;
using Rxdk.Xbox360.Dwarf;

if (args.Length < 1)
{
    Console.Error.WriteLine("usage: RxdkDwarf <title.elf> [--lines|--locals|--addr <hex>|--line <file:line>]");
    return 1;
}

string elf = args[0];
DwarfInfo info;
try { info = DwarfReader.Read(elf); }
catch (Exception e) { Console.Error.WriteLine("error: " + e.Message); return 1; }

int fnCount = 0; foreach (var _ in info.Functions) fnCount++;
Console.WriteLine($"{elf}: {info.Units.Count} unit(s), {fnCount} function(s)");

string mode = args.Length > 1 ? args[1] : "--funcs";
switch (mode)
{
    case "--addr":
        {
            ulong addr = ParseHex(args[2]);
            var fn = info.FunctionAt(addr);
            var ln = info.LineAt(addr);
            Console.WriteLine($"0x{addr:X8}  ->  {(fn?.Name ?? "<no function>")}" +
                              $"  {(ln != null ? $"{ln.File}:{ln.Line}" : "<no line>")}");
            break;
        }
    case "--line":
        {
            var parts = args[2].Split(':');
            var a = info.AddressFor(parts[0], int.Parse(parts[1]));
            Console.WriteLine(a != null ? $"{args[2]}  ->  0x{a:X8}" : $"{args[2]}  ->  <not found>");
            break;
        }
    case "--lines":
        foreach (var u in info.Units)
        {
            Console.WriteLine($"# {u.Name}");
            foreach (var r in u.Lines) Console.WriteLine("  " + r);
        }
        break;
    case "--locals":
        foreach (var f in info.Functions)
        {
            Console.WriteLine(f);
            foreach (var v in f.Variables) Console.WriteLine("    " + v);
        }
        break;
    default:
        foreach (var f in info.Functions) Console.WriteLine("  " + f);
        break;
}
return 0;

static ulong ParseHex(string s)
{
    s = s.Trim();
    if (s.StartsWith("0x") || s.StartsWith("0X")) s = s[2..];
    return ulong.Parse(s, NumberStyles.HexNumber);
}
