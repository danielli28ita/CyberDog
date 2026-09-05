# Publish the Release exe: copy to dist\ and the Desktop as CyberDog-<Version>.exe, and send every other
# CyberDog-*.exe / Jdog-*.exe in those two places to the Recycle Bin, so only one compiled file stays. ASCII only.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish.ps1 -Version 1.0
param([string]$Version = "1.0", [string]$Exe = ".\build\bin\Release\CyberDog.exe", [string]$Guide = ".\dist\使用说明.txt")
Add-Type -AssemblyName Microsoft.VisualBasic
$desktop = [Environment]::GetFolderPath("Desktop")
$name = "CyberDog-$Version.exe"
foreach ($dir in @((Resolve-Path ".\dist").Path, $desktop)) {
    foreach ($old in Get-ChildItem -LiteralPath $dir -Filter "*.exe" -File | Where-Object { $_.Name -like "CyberDog-*.exe" -or $_.Name -like "Jdog-*.exe" }) {
        if ($old.Name -ne $name) {
            [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($old.FullName, 'OnlyErrorDialogs', 'SendToRecycleBin')
            Write-Output ("recycled: {0}" -f $old.FullName)
        }
    }
    Copy-Item -LiteralPath $Exe -Destination (Join-Path $dir $name) -Force
    Write-Output ("published: {0}" -f (Join-Path $dir $name))
}
if (Test-Path -LiteralPath $Guide) {
    Copy-Item -LiteralPath $Guide -Destination (Join-Path $desktop "CyberDog-使用说明.txt") -Force
}
