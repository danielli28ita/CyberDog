# Print the taskbar window rect, the work area and the monitor size in physical pixels. ASCII only.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\taskbar_rect.ps1
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class TB {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SystemParametersInfo(uint a, uint b, out RECT r, uint f);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
}
"@
[TB]::SetProcessDPIAware() | Out-Null
$h = [TB]::FindWindow("Shell_TrayWnd", $null)
$r = New-Object TB+RECT
[TB]::GetWindowRect($h, [ref]$r) | Out-Null
$w = New-Object TB+RECT
[TB]::SystemParametersInfo(0x0030, 0, [ref]$w, 0) | Out-Null   # SPI_GETWORKAREA
Write-Output ("taskbar=({0},{1},{2},{3})" -f $r.l, $r.t, $r.r, $r.b)
Write-Output ("workarea=({0},{1},{2},{3})" -f $w.l, $w.t, $w.r, $w.b)
Write-Output ("screen={0}x{1}" -f [TB]::GetSystemMetrics(0), [TB]::GetSystemMetrics(1))
