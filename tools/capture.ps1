# Capture a region of the screen to PNG. Read-only for the desktop; writes one PNG.
# ASCII-only on purpose: PowerShell 5.1 reads a BOM-less file as ANSI, so no Chinese here.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\capture.ps1 -Out build\shot.png
#   -Width / -Height : size of the bottom-right region in physical pixels (default 900x520)
#   -Scale           : upscale factor for the saved image so small details are readable (default 2)

param(
    [string]$Out = "build\shot.png",
    [int]$Width = 900,
    [int]$Height = 520,
    [int]$Scale = 2
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

# Without this the process is DPI-virtualized and CopyFromScreen sees a scaled-down desktop.
Add-Type @"
using System.Runtime.InteropServices;
public static class DpiFix { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); }
"@
[DpiFix]::SetProcessDPIAware() | Out-Null

$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$x = $screen.Right - $Width
$y = $screen.Bottom - $Height

$bmp = New-Object System.Drawing.Bitmap $Width, $Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
$g.Dispose()

if ($Scale -gt 1) {
    $big = New-Object System.Drawing.Bitmap ($Width * $Scale), ($Height * $Scale)
    $g2 = [System.Drawing.Graphics]::FromImage($big)
    $g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g2.DrawImage($bmp, 0, 0, $Width * $Scale, $Height * $Scale)
    $g2.Dispose()
    $bmp.Dispose()
    $bmp = $big
}

$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "saved $Out  screen=$($screen.Width)x$($screen.Height) region=($x,$y) ${Width}x${Height} scale=$Scale"
