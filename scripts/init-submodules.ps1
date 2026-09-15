#Requires -Version 5.1
# Initialise the RXDK-360 submodules and apply our build-time patches.
#
#   vendor/picolibc  - the C23 libc (full checkout)
#
# Clang, lld, libcxx, libcxxabi and libunwind come from the llvm-project
# GitHub zip (scripts/fetch-clang.ps1 -> build/llvm), not a submodule.
# XexTool comes from Team-Resurgent/XexTool latest (scripts/fetch-xextool.ps1).

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path $PSScriptRoot -Parent
Set-Location $RepoRoot

$lp = @('-c', 'core.longpaths=true')

Write-Host 'Initializing picolibc (full checkout)...'
git @lp submodule update --init vendor/picolibc
if ($LASTEXITCODE -ne 0) { throw "submodule update (picolibc) failed ($LASTEXITCODE)" }

$treeMap = @{
    'picolibc' = 'vendor/picolibc'
}
$patchRoot = Join-Path $RepoRoot 'patches'
if (Test-Path $patchRoot) {
    Get-ChildItem $patchRoot -Directory | ForEach-Object {
        $tree = $treeMap[$_.Name]
        if (-not $tree) { return }
        Get-ChildItem $_.FullName -Filter '*.patch' | Sort-Object Name | ForEach-Object {
            git -C $tree apply --reverse --check $_.FullName 2>$null
            if ($LASTEXITCODE -eq 0) {
                Write-Host "  patch already applied: $($_.Name) -> $tree"
            }
            else {
                git -C $tree apply $_.FullName
                if ($LASTEXITCODE -ne 0) { throw "failed to apply patch $($_.Name) to $tree" }
                Write-Host "  applied patch: $($_.Name) -> $tree"
            }
        }
    }
}

Write-Host 'Submodule status:'
git submodule status
