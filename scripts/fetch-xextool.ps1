#Requires -Version 5.1
# Download Team-Resurgent/XexTool rolling "latest" XexTool-windows-x64.zip
# into build/xextool/XexTool.exe.
#
#   powershell -NoProfile -File scripts/fetch-xextool.ps1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path $PSScriptRoot -Parent
Set-Location $RepoRoot

$zipName = "XexTool-windows-x64.zip"
New-Item -ItemType Directory -Force -Path deps, build | Out-Null
$zipPath = Join-Path $RepoRoot "deps\$zipName"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

if (Get-Command gh -ErrorAction SilentlyContinue) {
    gh release download latest --repo Team-Resurgent/XexTool `
        --pattern $zipName --dir deps
} else {
    $url = "https://github.com/Team-Resurgent/XexTool/releases/download/latest/$zipName"
    Write-Host "gh not found; downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
}

$zip = Get-ChildItem $zipPath | Select-Object -First 1
if (-not $zip) { throw "$zipName missing after download" }

$tmp = Join-Path $RepoRoot "deps\xextool-unpack"
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
Expand-Archive $zip.FullName -DestinationPath $tmp -Force

$exe = Get-ChildItem $tmp -Recurse -Filter XexTool.exe | Select-Object -First 1
if (-not $exe) { throw "XexTool.exe missing from $zipName" }

$destDir = Join-Path $RepoRoot "build\xextool"
New-Item -ItemType Directory -Force -Path $destDir | Out-Null
Copy-Item $exe.FullName (Join-Path $destDir "XexTool.exe") -Force

Write-Host "XexTool -> $destDir\XexTool.exe ($($exe.Length) bytes)"
try {
    $banner = & (Join-Path $destDir "XexTool.exe") 2>&1 | Select-Object -First 1
    Write-Host $banner
} catch {
    Write-Host "XexTool.exe present (banner skipped: $($_.Exception.Message))"
}
