# Right-click at a physical screen point (opens / closes the stats panel when on the dog). ASCII only.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\right_click.ps1 -X 2300 -Y 1450
param([int]$X = 2300, [int]$Y = 1450)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class RC {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
}
"@
[RC]::SetProcessDPIAware() | Out-Null
[RC]::SetCursorPos($X, $Y) | Out-Null
Start-Sleep -Milliseconds 150
[RC]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero)   # right down
Start-Sleep -Milliseconds 60
[RC]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero)   # right up
Write-Output ("right-clicked at ({0},{1})" -f $X, $Y)
