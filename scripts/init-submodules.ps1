#Requires -Version 5.1
# Initialise the RXDK-360 submodules and apply our build-time patches.
#
#   vendor/picolibc      - the C23 libc (full checkout)
#   vendor/llvm-project  - the MS-PPC clang fork; sparse to the compiler cone
#                          plus the C++ runtime sources (libcxx/libcxxabi/libunwind)
#
# Patches under patches/<tree>/*.patch are applied to the matching submodule tree
# at the end. Idempotent: a patch already applied is detected and skipped, so this
# can be re-run safely. See patches/README.md and docs/runtime.md.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path $PSScriptRoot -Parent
Set-Location $RepoRoot

# core.longpaths must be on the command line -- a repo-level setting does not
# propagate to the child git processes that check out submodules, and the
# llvm-project tree has deep paths that otherwise fail on Windows.
$lp = @('-c', 'core.longpaths=true')

Write-Host 'Initializing picolibc (full checkout)...'
git @lp submodule update --init vendor/picolibc
if ($LASTEXITCODE -ne 0) { throw "submodule update (picolibc) failed ($LASTEXITCODE)" }

# llvm-project carries both the compiler and the C++ runtime sources. We never
# build the whole monorepo, so sparse-checkout to just the cones we use; configure
# it in the module store before the working tree is materialised so a full checkout
# never runs.
$llvm = 'vendor/llvm-project'
$cone = @('clang', 'lld', 'llvm', 'cmake', 'libc', 'third-party',
          'libcxx', 'libcxxabi', 'libunwind')

Write-Host "Configuring llvm-project sparse checkout ($($cone -join ' '))..."
git submodule init $llvm

$moduleDir = (git rev-parse --git-path "modules/$llvm").Trim()
if (Test-Path $moduleDir) {
    git -C $moduleDir config core.sparseCheckout true
    git -C $moduleDir config core.sparseCheckoutCone true
    $info = Join-Path $moduleDir 'info'
    if (-not (Test-Path $info)) { New-Item -ItemType Directory -Force $info | Out-Null }
    $patterns = @('/*', '!/*/') + ($cone | ForEach-Object { "/$_/" })
    Set-Content -Encoding ascii -Path (Join-Path $info 'sparse-checkout') -Value $patterns
}

git @lp submodule update --init $llvm
if ($LASTEXITCODE -ne 0) { throw "submodule update (llvm-project) failed ($LASTEXITCODE)" }

git -C $llvm sparse-checkout init --cone
git -C $llvm sparse-checkout set @cone

# Apply the tracked build-time patches. patches/<tree> maps to a submodule path.
$treeMap = @{
    'picolibc' = 'vendor/picolibc'
    'libcxx'   = 'vendor/llvm-project'
    'libcxxabi'= 'vendor/llvm-project'
    'libunwind'= 'vendor/llvm-project'
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
