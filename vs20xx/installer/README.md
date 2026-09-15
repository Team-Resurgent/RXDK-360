# RXDK-360 installer (Inno Setup)

`RXDK-360.iss` builds `RXDK-360-Setup.exe`, an installer that stands up an
**RXDK-360 SDK relocated to `C:\Program Files\RXDK-360`**, registered under its
own key/env so it lives **side by side** with a stock
`C:\Program Files (x86)\Microsoft Xbox 360 SDK`.

## Build

Build the unpacker and the VSIX first, then compile Setup. The VSIX already
contains the Xbox 360 platform, prebuilt net472 task DLLs, templates, and the
DAP. The target compiles nothing.

```
dotnet build vs20xx\installer\unpacker\RxdkXdkUnpacker.csproj -c Release
msbuild vs20xx\extension\Rxdk360.Vsix\Rxdk360.Vsix.csproj /restore /p:Configuration=Release
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" vs20xx\installer\RXDK-360.iss
```

CI (`.github/workflows/release.yml`) downloads `xbox360-windows-x64.zip` and
`xdvdfs.exe` from XDVDFS-TR, then publishes a rolling `latest` with the VSIX and
Setup.exe.

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
  `XenonSDK` keys, so it coexists with a stock XDK. Layout is `{app}\bin\win32`,
  `{app}\legacy\include`, `{app}\legacy\lib` (stock `lib\xbox` files, no `xbox\`
  folder), other libs under `{app}\lib\...`, clang under `{app}\modern`, and
  `xdvdfs.exe` at `{app}\bin`. The tree is `%XEDK%`-relative, so it runs from
  the new path.
- **Side-by-side registration** — writes `InstallPath` (`{app}`) and `XdkPath`
  (`{app}\legacy`) under `HKLM\SOFTWARE\TeamResurgent\RXDK-360`, which the
  RXDK-360 MSBuild platform reads first, and optionally the `RXDK360` machine
  env var. The stock `XEDK` / `Xbox\2.0\SDK` keys are left untouched.
- **VS integration** — Setup copies the Xbox 360 platform + net472 task DLLs into
  every VS 2022 (`v170`) and VS 2026/18 (`v170` + `v180`) `VC\Platforms` folder,
  then VSIXInstaller installs the packaged VSIX (templates + DAP). Nothing is
  compiled on the target.
- **Start menu** — RXDK-360 group: Command Prompt (`xdkvars.bat`), Xbox
  Neighborhood, PIX, Documentation.
- **Prior-install detection** — `InitializeSetup` finds an existing RXDK-360 and
  offers to uninstall it first (clean upgrade).
- **Uninstall** — removes the VSIX + platform folders (`vsuninstall`),
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
- VS integration copies prebuilt net472 task DLLs and a packaged VSIX. The
  target needs Visual Studio, not the .NET SDK.
- Only build essentials (`bin`, `include`, `lib`, `source`, `doc`) are relocated;
  add subtrees in `[Files]` to install the full SDK.
