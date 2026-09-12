# RXDK-360 — Xbox 360 XDK on modern Visual Studio

Port of the stock **Xbox 360 XDK** (which shipped Visual Studio 2010 integration)
to **modern Visual Studio** (VS2022 / v170 and VS "18"/2026 / v170+v180), so the
unmodified XDK compiler and libraries build Xbox 360 titles from current MSBuild.

This is the "make the stock XDK work" layer. It does **not** involve the RXDK-360
custom toolchain (clang/lld/coff2elf) — it drives the XDK's own
`cl.exe`/`link.exe`/`lib.exe`/`imagexex.exe` through a modern MSBuild platform.

## Status

| Piece | State |
|---|---|
| Task assembly rebuilt from source against v170 CPPTasks | ✅ builds clean |
| `RXDK-360` MSBuild platform (v170/v180) loads in modern VS | ✅ |
| **StaticLibrary** build (cl → lib) end to end | ✅ produces a PPCBE `.lib` |
| Application build (cl → link → imagexex → `.xex`) | ⏳ next |
| VS project/item templates | ⏳ |
| Remote debugger (VSPackage) | ⏳ (deferred; separate native VSIX) |
| VSIX bundling all of the above | ⏳ |

Verified: a minimal `.vcxproj` with `<Platform>RXDK-360</Platform>` builds under
`VS2022\MSBuild\Current\Bin\MSBuild.exe`, invoking the XDK PowerPC `cl.exe`
(v16.00) and `Lib.exe`; `dumpbin /headers` reports `1F2 machine (PPCBE)`.

## Why the stock DLL had to be rebuilt

The XDK's MSBuild tasks live in `Microsoft.Xna.Xbox360.Build.dll`, whose
`CL`/`Link` tasks derive from **`Microsoft.Build.CPPTasks.Common, Version=4.0.0.0`**
— the VS2010-era VC MSBuild task base, which modern Visual Studio no longer
ships. So the stock DLL fails to bind under VS2022/2026 MSBuild.

Because it is a small, pure-.NET assembly, it was decompiled and reconstructed as
source (`tasks/Rxdk.Xbox360.Build/`) and recompiled against the installed
toolset's `Microsoft.Build.CPPTasks.Common` (v170). All twelve overridden VC-task
properties are still `public virtual` with identical types in v170, so the task
logic is faithful to the original; only two v170 API renames were applied
(`logPrivate` → `LogPrivate`, `ToolSwitch.MultiValues` → `MultipleValues`). The
`CL`/`Link` classes are renamed `Xbox360CL`/`Xbox360Link` so their `UsingTask`
registration does not collide with the stock v170 `CL`/`Link` tasks.

## What the port changes vs. the stock v4.0 platform

The stock platform (`.../MSBuild/Microsoft.Cpp/v4.0/Platforms/Xbox 360`) was
copied to `Platforms/RXDK-360/` and adapted for the modern VC platform contract:

- Entry files renamed to the modern convention: `Microsoft.Cpp.Xbox 360.*` →
  `Platform.Default.props` / `Platform.props` / `Platform.targets`; toolset files
  → `PlatformToolsets/2010-01/Toolset.props` / `Toolset.targets`.
- All internal `\Platforms\Xbox 360\` references retargeted to `\Platforms\RXDK-360\`.
- `UsingTask` assembly retargeted from the stock XDK DLL to the rebuilt
  `Rxdk.Xbox360.Build.dll` (bundled next to the platform files).
- Compatibility shims for things the modern MSBuild expects that the 2010 core
  used to supply (this platform is intentionally **not** an MSVC toolset, so it
  does not import `Microsoft.Cpp.Common.props` / `Microsoft.Cpp.MSVC.Toolset.*`):
  - `ClCompile` default output-path metadata (`ProgramDataBaseFileName`,
    `ObjectFileName`, ...) — else `ComputeCLInputPDBName` fails MSB4096.
  - `$(LinkCompiled)`/`$(LibCompiled)` signal properties by `ConfigurationType`
    — else the `_Lib`/`_Link` build orchestration is skipped and nothing is
    archived/linked.
  - `$(ProjectName)`/`$(TargetName)` defaults — else the output is named `.lib`.
  - `DependsOnTargets="ComputeLinkSwitches"` / `$(ComputeLibInputsTargets)` on the
    overridden `Link`/`Lib` targets — else `@(Link)`/`@(Lib)` are never populated.

The XDK itself is untouched, and the compiler/headers/libs come from the stock
install via `HKLM\...\Xbox\2.0\SDK@InstallPath`.

## Install (developer, pre-VSIX)

From an **elevated** PowerShell (writes under the VS install):

```powershell
powershell -ExecutionPolicy Bypass -File vs20xx\install.ps1
```

This builds the task assembly, stages it, and copies the `RXDK-360` platform into
every detected VS install's `MSBuild\Microsoft\VC\{v170,v180}\Platforms`.
`-Uninstall` removes them.

Then a project uses `<Platform>RXDK-360</Platform>` with
`<PlatformToolset>2010-01</PlatformToolset>` (see `tests/hello`).

## Layout

```
vs20xx/
  Platforms/RXDK-360/        the ported MSBuild platform (source of truth)
  tasks/Rxdk.Xbox360.Build/  the rebuilt-from-source CL/Link/ImageXex/Deploy tasks
  tests/hello/               minimal StaticLibrary smoke test
  install.ps1                build + install into detected VS installs
```
