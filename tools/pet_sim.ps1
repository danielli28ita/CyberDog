# Simulate a slow stroke on the dog (petting): drift a few px every 30 ms over the given point.
# Default presses the left button first (1.0-1.6 petting); -NoPress just hovers (1.7+ petting needs no button).
# -Fast sweeps back and forth quickly instead (hitting).
# ASCII only. Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\pet_sim.ps1 -X 2230 -Y 1440 -Seconds 2.5 [-NoPress] [-Fast]
param([int]$X = 2230, [int]$Y = 1440, [double]$Seconds = 2.5, [switch]$NoPress, [switch]$Fast)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Sim {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
}
"@
[Sim]::SetProcessDPIAware() | Out-Null
[Sim]::SetCursorPos($X, $Y) | Out-Null
Start-Sleep -Milliseconds 120
if (-not $NoPress) { [Sim]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero) }   # left down
$steps = [int]($Seconds * 1000 / 30)
for ($i = 0; $i -lt $steps; $i++) {
    if ($Fast) {
        $dx = if (($i % 2) -eq 0) { -45 } else { 45 }
        $dy = 0
    } else {
        $dx = [int](12 * [Math]::Sin($i / 6.0))
        $dy = [int](4 * [Math]::Cos($i / 9.0))
    }
    [Sim]::SetCursorPos($X + $dx, $Y + $dy) | Out-Null
    Start-Sleep -Milliseconds 30
}
if (-not $NoPress) { [Sim]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero) }   # left up
Write-Output ("moved over ({0},{1}) for {2}s press={3} fast={4}" -f $X, $Y, $Seconds, (-not $NoPress), [bool]$Fast)
