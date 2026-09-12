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
- **Extract + relocate → `{app}`** — the bundled `RxdkXdkUnpacker.exe` (see
  `unpacker/`) unpacks the setup EXE to a temp dir (it walks the setup's chain of
  concatenated MS cabinets; the payload lands under an `XDK\` prefix), and the
  `external` `[Files]` entries relocate `XDK\{bin,include,lib,source,doc}` to
  `C:\Program Files\RXDK-360`. The tree is fully `%XEDK%`-relative, so it runs
  correctly from the new path.
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
