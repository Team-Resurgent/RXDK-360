# SPDX-License-Identifier: GPL-3.0-or-later
# Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
#
# Installs the RXDK-360 "port the stock Xbox 360 XDK to modern Visual Studio"
# MSBuild platform into every detected Visual Studio's VC platform folders.
#
# This mirrors, by hand, what the eventual RXDK-360 VSIX will do: drop the
# ported "RXDK-360" platform (Platform.props/targets, rule .xml, toolset, and
# the recompiled Rxdk.Xbox360.Build.dll task assembly) into
#   <VS>\MSBuild\Microsoft\VC\<toolset>\Platforms\RXDK-360
# so a .vcxproj with <Platform>RXDK-360</Platform> loads and builds against the
# stock XDK cl.exe/link.exe/lib.exe/imagexex.exe.
#
# Run from an ELEVATED PowerShell (writes under Program Files):
#   powershell -ExecutionPolicy Bypass -File install.ps1
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall
[CmdletBinding()]
param(
    [switch]$Uninstall,
    # Which VC toolset platform dirs to target. VS2022 ships v170; VS "18"/2026
    # ships v170 and v180.
    [string[]]$ToolsetDirs = @('v170', 'v180')
)

$ErrorActionPreference = 'Stop'
$src = Join-Path $PSScriptRoot 'Platforms\RXDK-360'
if (-not (Test-Path $src)) { throw "platform source not found: $src" }

# Build the RXDK-360 task assembly from source and stage it next to the platform
# files, so the installed platform is self-contained. (The VSIX will bundle a
# prebuilt copy instead.) Skipped for -Uninstall.
if (-not $Uninstall) {
    $taskProj = Join-Path $PSScriptRoot 'tasks\Rxdk.Xbox360.Build\Rxdk.Xbox360.Build.csproj'
    $taskDll  = Join-Path $PSScriptRoot 'tasks\Rxdk.Xbox360.Build\bin\Release\net472\Rxdk.Xbox360.Build.dll'
    Write-Host "building task assembly..."
    & dotnet build $taskProj -c Release -v q
    if ($LASTEXITCODE -ne 0) { throw "task assembly build failed" }
    Copy-Item -Force $taskDll (Join-Path $src 'Rxdk.Xbox360.Build.dll')
    Write-Host "staged  $(Join-Path $src 'Rxdk.Xbox360.Build.dll')"
}

function Get-VsInstalls {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; is Visual Studio installed?" }
    & $vswhere -all -prerelease -products * -property installationPath 2>$null
}

$targets = @()
foreach ($vs in (Get-VsInstalls)) {
    foreach ($ts in $ToolsetDirs) {
        $platRoot = Join-Path $vs "MSBuild\Microsoft\VC\$ts\Platforms"
        if (Test-Path $platRoot) { $targets += (Join-Path $platRoot 'RXDK-360') }
    }
}
if ($targets.Count -eq 0) { throw "no VC platform folders found under any Visual Studio install" }

foreach ($dst in $targets) {
    if ($Uninstall) {
        if (Test-Path $dst) { Remove-Item -Recurse -Force $dst; Write-Host "removed  $dst" }
        else { Write-Host "absent   $dst" }
        continue
    }
    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item -Recurse -Force (Join-Path $src '*') $dst
    Write-Host "installed $dst"
}
Write-Host "`nDone. Platform name to use in a .vcxproj: RXDK-360 (toolset: 2010-01)."
