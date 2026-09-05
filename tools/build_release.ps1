# One-step Release build: configure (if needed), build, run the core tests, then publish so the Desktop
# copy is updated automatically and only one CyberDog-<Version>.exe remains in dist\ and on the Desktop.
# Does NOT export, commit, tag or upload anything. ASCII only.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_release.ps1 -Version 1.4 [-SkipTests]
param([Parameter(Mandatory = $true)][string]$Version, [switch]$SkipTests)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root

# Version in code must match what we publish; refuse otherwise so exe name and kVersion never drift.
$m = Select-String -Path 'overlay\src\main_app.cpp' -Pattern 'kVersion\s*=\s*"([0-9.]+)"' | Select-Object -First 1
$code = if ($m) { $m.Matches[0].Groups[1].Value } else { '' }
if ($code -ne $Version) { throw ("kVersion in overlay/src/main_app.cpp is '{0}', asked to publish '{1}'. Fix the code (and cyberdog.rc) first." -f $code, $Version) }
$rc = Select-String -Path 'overlay\res\cyberdog.rc' -Pattern 'FILEVERSION\s+(\d+),(\d+)' | Select-Object -First 1
if ($rc) {
    $rcVer = '{0}.{1}' -f $rc.Matches[0].Groups[1].Value, $rc.Matches[0].Groups[2].Value
    if ($rcVer -ne $Version) { throw ("FILEVERSION in cyberdog.rc is {0}, expected {1}" -f $rcVer, $Version) }
}

if (-not (Test-Path 'build\CMakeCache.txt')) {
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
}
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

if (-not $SkipTests) {
    & .\build\bin\Release\pet_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'pet_tests failed; not publishing' }
}

# Running instances hold the old exe on the Desktop; ask them to close so the copy succeeds.
$running = Get-Process -Name 'CyberDog*' -ErrorAction SilentlyContinue
if ($running) {
    $running | ForEach-Object { $_.CloseMainWindow() | Out-Null }
    Start-Sleep -Seconds 2
}

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'publish.ps1') -Version $Version
if ($LASTEXITCODE -ne 0) { throw 'publish failed' }
Write-Output ("done: Desktop and dist now hold CyberDog-{0}.exe only. Next: measure (tools\measure.ps1) before any release; upload only when the author says so." -f $Version)
