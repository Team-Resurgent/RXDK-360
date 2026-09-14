<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# RXDK-360 modern devkit debugger

Source-level debugging of a **modern** (clang/LLVM) RXDK-360 title on a real
Xbox 360 devkit. Three pieces:

| Project | What it is |
|---|---|
| `Rxdk.Xbox360.Dwarf` | Reads the `.debug_*` sections of a Debug build's `.elf` into functions, a line table and locals (address ↔ source, both ways). Standalone .NET. |
| `Rxdk.Xbox360.Pdb` | Reads C13 PDB/XDB from the legacy 2010-01 toolset (`Rxdk.Xbox360.Pdb.dll`). |
| `Rxdk.Xbox360.Xbdm` | Managed façade over `%XEDK%\bin\win32\xbdm.dll` (P/Invoke of the `Dm*` APIs in `include\win32\xbdm.h`): connect, deploy, reboot-into-title-stopped, breakpoints, stop/go, memory, PPC registers, debug-event notifications. **Windows x86** (the host DLL is PE32). |
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

(Register `Rxdk.Xbox360.DebugAdapter.exe` as the `rxdk360` debug type via a small VS Code
extension, or launch it as an external DAP server.) Wiring the same adapter into
a Visual Studio `.vcxproj` F5 is a further integration step.

## Status

The symbol reader and the DAP/symbol glue are validated offline; the live path
(connect → deploy → break → step → read locals) needs a real devkit to exercise.

The XBDM host DLL is **32-bit**. `Rxdk.Xbox360.Xbdm` / `Rxdk.Xbox360.DebugAdapter` therefore target
`win-x86` and load the RXDK-360 copy first:

`%RXDK360%\bin\win32\xbdm.dll`  (`InstallPath` / `XdkPath` = `{app}`)

falling back to stock `%XEDK%\bin\win32\xbdm.dll`. The kit name is
`$(DefaultConsole)` = `HKCU\SOFTWARE\Microsoft\XenonSDK\XboxName`.
Modern (`{app}\modern`) is the clang/LLVM sidecar and does not ship `xbdm.dll`.
