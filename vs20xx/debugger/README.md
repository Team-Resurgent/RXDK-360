<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# RXDK-360 modern devkit debugger

Source-level debugging of a **modern** (clang/LLVM) RXDK-360 title on a real
Xbox 360 devkit. Three pieces:

| Project | What it is |
|---|---|
| `Rxdk.Xbox360.Dwarf` | Reads the `.debug_*` sections of a Debug build's `.elf` into functions, a line table and locals (address ↔ source, both ways). Standalone .NET. |
| `Rxdk.Xbox360.Xbdm` | Managed façade over the XDK's `xbdm.dll` (P/Invoke of the DM\* APIs): connect, deploy, reboot-into-title-stopped, breakpoints, stop/go, memory, PPC registers, debug-event notifications. Windows-only. |
| `Rxdk.Xbox360.DebugAdapter` | A Debug Adapter Protocol server that glues the two: F5-to-devkit steps source. |

A Debug build emits DWARF and keeps `<title>.elf` beside `<title>.xex` (the
symbol file); the adapter maps the running XEX's addresses back to source through
that DWARF.

## Offline self-checks (no devkit)

```
dotnet run --project Rxdk.Xbox360.Dwarf        -- <title>.elf --locals
dotnet run --project Rxdk.Xbox360.Xbdm         -- info
dotnet run --project Rxdk.Xbox360.DebugAdapter -- --selftest <title>.elf
```

## Using it from VS Code

Build the adapter, then add a debug configuration that launches it (a small
`.vscode/launch.json`), pointing at the console and the built title:

```json
{
  "version": "0.2.0",
  "configurations": [{
    "type": "rxdk360",
    "request": "launch",
    "name": "Debug on devkit",
    "program": "${workspaceFolder}/out/Debug/MyTitle.xex",
    "symbols": "${workspaceFolder}/out/Debug/MyTitle.elf",
    "console": "192.168.1.50",
    "stopAtEntry": true,
    "debugServer": 0
  }]
}
```

(Register `RxdkDebugAdapter.exe` as the `rxdk360` debug type via a small VS Code
extension, or launch it as an external DAP server.) Wiring the same adapter into
a Visual Studio `.vcxproj` F5 is a further integration step.

## Status

The symbol reader and the DAP/symbol glue are validated offline; the live path
(connect → deploy → break → step → read locals) needs a real devkit to exercise.
