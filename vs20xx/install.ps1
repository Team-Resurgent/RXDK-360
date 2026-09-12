# SPDX-License-Identifier: GPL-3.0-or-later
# Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
#
# RXDK-360 single-installer: sets up everything needed to build Xbox 360 titles
# with the stock XDK toolchain on modern Visual Studio.
#
#   1. builds the RXDK-360 MSBuild task assembly from source,
#   2. installs the RXDK-360 platform (+ task assembly) into every detected
#      Visual Studio's VC\<toolset>\Platforms  (needs elevation - Program Files),
#   3. builds and installs the RXDK-360 VSIX (project templates) into each VS.
#
# The script self-elevates if not already running as administrator.
#
#   powershell -ExecutionPolicy Bypass -File install.ps1            # install all
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall # remove all
#   ... -SkipVsix        install the platform only (no templates)
#   ... -SkipPlatform    install the VSIX only (platform already present)
[CmdletBinding()]
param(
    [switch]$Uninstall,
    [switch]$SkipVsix,
    [switch]$SkipPlatform,
    # VC toolset platform dirs to target. VS2022 ships v170; VS "18"/2026 v170+v180.
    [string[]]$ToolsetDirs = @('v170', 'v180')
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$src  = Join-Path $root 'Platforms\RXDK-360'
$vsixProj = Join-Path $root 'extension\Rxdk360.Vsix\Rxdk360.Vsix.csproj'
$vsixOut  = Join-Path $root 'extension\Rxdk360.Vsix\bin\Release\Rxdk360.Vsix.vsix'
$vsixId   = 'Rxdk360.Vsix.add38e43-cc73-4417-9c4b-e2d43131ab14'

# --- self-elevate (the platform copy writes under Program Files) -------------
function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}
if (-not (Test-Admin)) {
    Write-Host "elevating..."
    $psi = @('-ExecutionPolicy','Bypass','-NoProfile','-File',"`"$PSCommandPath`"")
    if ($Uninstall)    { $psi += '-Uninstall' }
    if ($SkipVsix)     { $psi += '-SkipVsix' }
    if ($SkipPlatform) { $psi += '-SkipPlatform' }
    Start-Process powershell -Verb RunAs -Wait -ArgumentList $psi
    return
}

function Get-VsInstalls {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; is Visual Studio installed?" }
    & $vswhere -all -prerelease -products * -property installationPath 2>$null
}
$vsInstalls = @(Get-VsInstalls)
if ($vsInstalls.Count -eq 0) { throw "no Visual Studio install found" }

# --- 1 + 2: task assembly + platform ---------------------------------------
if (-not $SkipPlatform) {
    if (-not (Test-Path $src)) { throw "platform source not found: $src" }
    if (-not $Uninstall) {
        $taskProj = Join-Path $root 'tasks\Rxdk.Xbox360.Build\Rxdk.Xbox360.Build.csproj'
        $taskDll  = Join-Path $root 'tasks\Rxdk.Xbox360.Build\bin\Release\net472\Rxdk.Xbox360.Build.dll'
        Write-Host "building task assembly..."
        & dotnet build $taskProj -c Release -v q
        if ($LASTEXITCODE -ne 0) { throw "task assembly build failed" }
        Copy-Item -Force $taskDll (Join-Path $src 'Rxdk.Xbox360.Build.dll')
    }
    foreach ($vs in $vsInstalls) {
        foreach ($ts in $ToolsetDirs) {
            $platRoot = Join-Path $vs "MSBuild\Microsoft\VC\$ts\Platforms"
            if (-not (Test-Path $platRoot)) { continue }
            $dst = Join-Path $platRoot 'RXDK-360'
            if ($Uninstall) {
                if (Test-Path $dst) { Remove-Item -Recurse -Force $dst; Write-Host "removed   $dst" }
                continue
            }
            if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
            New-Item -ItemType Directory -Force -Path $dst | Out-Null
            Copy-Item -Recurse -Force (Join-Path $src '*') $dst
            Write-Host "installed $dst"
        }
    }
}

# --- 3: VSIX (project templates) -------------------------------------------
if (-not $SkipVsix) {
    foreach ($vs in $vsInstalls) {
        $vsixInstaller = Join-Path $vs 'Common7\IDE\VSIXInstaller.exe'
        if (-not (Test-Path $vsixInstaller)) { continue }
        if ($Uninstall) {
            Write-Host "uninstalling VSIX from $vs ..."
            & $vsixInstaller /quiet /uninstall:$vsixId 2>$null | Out-Null
            continue
        }
        if (-not (Test-Path $vsixOut)) {
            Write-Host "building VSIX..."
            $msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
                -latest -prerelease -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
            if (-not $msbuild) { throw "MSBuild not found to build the VSIX" }
            & $msbuild $vsixProj /restore /p:Configuration=Release /v:m /nologo
            if ($LASTEXITCODE -ne 0) { throw "VSIX build failed" }
        }
        Write-Host "installing VSIX into $vs ..."
        & $vsixInstaller /quiet "$vsixOut"
    }
}

Write-Host "`nDone. New Project -> 'Xbox 360 Title' / 'Xbox 360 Static Library',"
Write-Host "or set <Platform>RXDK-360</Platform> + <PlatformToolset>2010-01</PlatformToolset>."
