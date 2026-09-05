# Reproduce "click the dog, then nothing is clickable" and report what the system says.
# ASCII only (PowerShell 5.1 reads BOM-less files as ANSI).
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\probe_input.ps1 -DogX 2200 -DogY 1320
# Steps: save cursor -> move to dog, press/release left button -> move to an empty spot ->
#        report WindowFromPoint (class), capture window, foreground window -> restore cursor.

param(
    [int]$DogX = 2200,
    [int]$DogY = 1320,
    [int]$EmptyX = 900,
    [int]$EmptyY = 700
)

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Probe {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern IntPtr GetCapture();
    [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint tid, ref GUITHREADINFO info);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x; public int y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
    [StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO {
        public uint cbSize; public uint flags; public IntPtr hwndActive; public IntPtr hwndFocus;
        public IntPtr hwndCapture; public IntPtr hwndMenuOwner; public IntPtr hwndMoveSize; public IntPtr hwndCaret; public RECT rcCaret; }
    public static string Name(IntPtr h) {
        if (h == IntPtr.Zero) return "(null)";
        var c = new StringBuilder(128); GetClassName(h, c, 128);
        var t = new StringBuilder(128); GetWindowText(h, t, 128);
        return c.ToString() + " \"" + t.ToString() + "\" 0x" + h.ToString("X");
    }
    public static string HitTest(IntPtr h, int x, int y) {
        // WM_NCHITTEST = 0x84, lParam = y<<16 | x (screen coords)
        long r = SendMessage(h, 0x84, IntPtr.Zero, (IntPtr)((y << 16) | (x & 0xFFFF))).ToInt64();
        return r.ToString();
    }
}
"@
[Probe]::SetProcessDPIAware() | Out-Null

$orig = New-Object Probe+POINT
[Probe]::GetCursorPos([ref]$orig) | Out-Null

function Report($label, $x, $y) {
    $p = New-Object Probe+POINT; $p.x = $x; $p.y = $y
    $w = [Probe]::WindowFromPoint($p)
    $gi = New-Object Probe+GUITHREADINFO
    $gi.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($gi)
    [Probe]::GetGUIThreadInfo(0, [ref]$gi) | Out-Null
    Write-Output ("[{0}] point=({1},{2})" -f $label, $x, $y)
    Write-Output ("   WindowFromPoint : " + [Probe]::Name($w))
    Write-Output ("   Foreground      : " + [Probe]::Name([Probe]::GetForegroundWindow()))
    Write-Output ("   GUI capture     : " + [Probe]::Name($gi.hwndCapture) + "  flags=0x" + $gi.flags.ToString("X"))
    Write-Output ("   GUI active/focus: " + [Probe]::Name($gi.hwndActive) + " / " + [Probe]::Name($gi.hwndFocus))
}

Report "before" $EmptyX $EmptyY

# click the dog
[Probe]::SetCursorPos($DogX, $DogY) | Out-Null
Start-Sleep -Milliseconds 300
[Probe]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)   # LEFTDOWN
Start-Sleep -Milliseconds 120
[Probe]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)   # LEFTUP
Start-Sleep -Milliseconds 300
Report "after click on dog, cursor on dog" $DogX $DogY

# move away
[Probe]::SetCursorPos($EmptyX, $EmptyY) | Out-Null
Start-Sleep -Milliseconds 500
Report "after moving away" $EmptyX $EmptyY

# ask the overlay directly what it says for the empty point
$p = New-Object Probe+POINT; $p.x = $EmptyX; $p.y = $EmptyY
$w = [Probe]::WindowFromPoint($p)
Write-Output ("   overlay WM_NCHITTEST at empty point: " + [Probe]::HitTest($w, $EmptyX, $EmptyY) + "  (HTTRANSPARENT=-1, HTCLIENT=1)")

[Probe]::SetCursorPos($orig.x, $orig.y) | Out-Null
