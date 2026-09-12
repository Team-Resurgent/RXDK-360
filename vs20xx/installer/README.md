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
acquires it at install time from the user's own XDK.

## What it does

- **Source page** — point at an existing XDK folder (defaults to `%XEDK%`) or the
  XDK setup EXE.
- **Payload → `{app}`** — `CopyTree` relocates the XDK to `C:\Program Files\RXDK-360`.
  The tree is fully `%XEDK%`-relative, so it runs correctly from the new path.
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

## Open item — installing directly from the setup EXE

The setup EXE is an **InstallShield self-extractor** (its payload is InstallShield's
own archive format, not a plain MS cabinet, so 7-Zip won't unpack it directly).
`AcquireXdkPayload()` currently implements **Strategy A** (relocate from an
existing XDK folder), which is robust because the tree is relocatable. Strategy B
(run the setup EXE to a staging dir with the right InstallShield silent/extract
switches, then relocate) is stubbed with a clear message and is the next piece to
pin down against the real EXE.
