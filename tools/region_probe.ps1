# Watch the CyberDog overlay window: visible flag and the bounding box of its window region.
# The region clips the picture, so an empty region while the window is visible means the dog vanished.
# ASCII only. Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\region_probe.ps1 -Count 100 -IntervalMs 100
param([int]$Count = 50, [int]$IntervalMs = 100)
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class RP {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowRgn(IntPtr h, IntPtr rgn);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateRectRgn(int a, int b, int c, int d);
    [DllImport("gdi32.dll")] public static extern int GetRgnBox(IntPtr rgn, out RECT r);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr o);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
    public static IntPtr Find(string cls) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            var sb = new StringBuilder(128); GetClassName(h, sb, 128);
            if (sb.ToString() == cls) { found = h; return false; }
            return true; }, IntPtr.Zero);
        return found;
    }
    public static string Describe(IntPtr h) {
        if (h == IntPtr.Zero) return "no-window";
        IntPtr rgn = CreateRectRgn(0, 0, 0, 0);
        int kind = GetWindowRgn(h, rgn);   // 0 = ERROR (no region), 1 = NULLREGION, 2 = SIMPLE, 3 = COMPLEX
        RECT r; GetRgnBox(rgn, out r);
        DeleteObject(rgn);
        string vis = IsWindowVisible(h) ? "visible" : "hidden";
        return vis + " rgnKind=" + kind + " box=(" + r.l + "," + r.t + "," + r.r + "," + r.b + ") " + (r.r - r.l) + "x" + (r.b - r.t);
    }
}
"@
$empty = 0; $hidden = 0
for ($i = 0; $i -lt $Count; $i++) {
    $h = [RP]::Find("PetOverlayWindow")
    $d = [RP]::Describe($h)
    if ($d -match "hidden") { $hidden++ }
    if ($d -match " 0x0") { $empty++ }
    Write-Output ("{0:HH:mm:ss.fff} {1}" -f (Get-Date), $d)
    if ($i + 1 -lt $Count) { Start-Sleep -Milliseconds $IntervalMs }
}
Write-Output ("summary: samples={0} hidden={1} emptyRegion={2}" -f $Count, $hidden, $empty)
