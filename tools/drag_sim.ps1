# Simulate dragging the dog: press left at (X,Y), move linearly by (DX,DY) over Seconds, release. ASCII only.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\drag_sim.ps1 -X 2300 -Y 1470 -DX -300 -DY -250 -Seconds 1.2
param([int]$X = 2300, [int]$Y = 1470, [int]$DX = -300, [int]$DY = -250, [double]$Seconds = 1.2)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Drag {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
}
"@
[Drag]::SetProcessDPIAware() | Out-Null
[Drag]::SetCursorPos($X, $Y) | Out-Null
Start-Sleep -Milliseconds 150
[Drag]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
$steps = [int]($Seconds * 1000 / 20)
for ($i = 1; $i -le $steps; $i++) {
    $t = $i / $steps
    [Drag]::SetCursorPos($X + [int]($DX * $t), $Y + [int]($DY * $t)) | Out-Null
    Start-Sleep -Milliseconds 20
}
Start-Sleep -Milliseconds 100
[Drag]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Write-Output ("dragged from ({0},{1}) by ({2},{3})" -f $X, $Y, $DX, $DY)
