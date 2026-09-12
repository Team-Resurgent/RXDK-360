# RXDK-360 installer (Inno Setup)

`RXDK-360.iss` builds `RXDK-360-Setup.exe`, an installer that stands up an
**RXDK-360 SDK relocated to `C:\Program Files\RXDK-360`**, registered under its
own key/env so it lives **side by side** with a stock
`C:\Program Files (x86)\Microsoft Xbox 360 SDK`.

## Build

```
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" RXDK-360.iss
```

The XDK payload is **not** bundled (licensed Microsoft content); the installer
acquires it at install time from the user's own XDK **setup EXE**.

## What it does

- **Source page** — point at your Xbox 360 XDK setup EXE (e.g.
  `XDKSetupXenon<version>.exe`).
- **Manifest-driven XDK install → `{app}`** — `{app}\tools\RxdkXdkUnpacker.exe
  install <setup.exe> {app}` unpacks the setup's cabinet chain and replays the
  original installer's `manifest.csv`, **relocated for RXDK-360**: files (by
  destination token: `XDK`→`{app}`, `SYSTEM_DIR`→System32, …), registry, the
  RXDK-360 Start-menu shortcuts, and the **Xbox 360 Neighborhood shell extension**
  (`xeshlext.dll`, self-registered). It never rewrites the stock `Xbox\2.0\SDK` /
  `XenonSDK` keys, so it coexists with a stock XDK. The tree is `%XEDK%`-relative,
  so it runs from the new path.
- **Side-by-side registration** — writes `HKLM\SOFTWARE\TeamResurgent\RXDK-360\InstallPath`
  (what the RXDK-360 MSBuild platform's `Toolset.props` now reads first) and,
  optionally, the `RXDK360` machine env var. The stock `XEDK` /
  `Xbox\2.0\SDK` key are left untouched.
- **VS integration** — runs the bundled `vsintegration\install.ps1` to build the
  per-toolset task assemblies, install the `RXDK-360` platform into each VS, and
  install the project-template VSIX. Because the platform reads the RXDK-360 key,
  it targets `{app}` automatically.
- **Start menu** — RXDK-360 group: Command Prompt (`xdkvars.bat`), Xbox
  Neighborhood, PIX, Documentation.
- **Prior-install detection** — `InitializeSetup` finds an existing RXDK-360 and
  offers to uninstall it first (clean upgrade).
- **Uninstall** — removes the VSIX + platform folders (`install.ps1 -Uninstall`),
  the registry key/env, the Start-menu group, and the `{app}` payload.

## Assets

`Icon.ico`, `WizardImage.bmp`, `WizardSmallImage.bmp` (wizard art) and
are bundled. `Uninstall.iss` carries the shared `IsUpgrade` /
`UnInstallOldVersion` / `EnvironmentKey` helpers. The extractor is our own
`unpacker/RxdkXdkUnpacker.exe` (build it before compiling the installer).

## Notes

- The Xbox 360 XDK setup EXE is a PE stub + **15 concatenated standard MS
  cabinets** (6539 files). Generic tools (7-Zip) extract only the first cabinet
  (bin\win32); `RxdkXdkUnpacker` walks the whole chain via the Windows FDI API.
- The VS integration ([Run] `install.ps1`) needs the .NET SDK and a VS 2022/2026
  install on the target. A future revision may bundle prebuilt task DLLs + VSIX
  to drop that build-time dependency.
- Only build essentials (`bin`, `include`, `lib`, `source`, `doc`) are relocated;
  add subtrees in `[Files]` to install the full SDK.
