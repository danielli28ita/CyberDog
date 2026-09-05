# Print the colors of one screen column between two rows (physical pixels). Used to find where the taskbar really starts. ASCII only.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\pixel_column.ps1 -X 2000 -Y0 1515 -Y1 1545
param([int]$X = 2000, [int]$Y0 = 1515, [int]$Y1 = 1545)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System.Runtime.InteropServices;
public static class DpiFix2 { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); }
"@
[DpiFix2]::SetProcessDPIAware() | Out-Null
$h = $Y1 - $Y0 + 1
$bmp = New-Object System.Drawing.Bitmap 1, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($X, $Y0, 0, 0, (New-Object System.Drawing.Size 1, $h))
for ($i = 0; $i -lt $h; $i++) {
    $c = $bmp.GetPixel(0, $i)
    Write-Output ("y={0} rgb=({1},{2},{3})" -f ($Y0 + $i), $c.R, $c.G, $c.B)
}
$g.Dispose(); $bmp.Dispose()
