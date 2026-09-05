# Print SHQueryUserNotificationState, the foreground window and whether the CyberDog overlay is visible. ASCII only.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\quns.ps1 [-Count 10] [-IntervalMs 500]
param([int]$Count = 1, [int]$IntervalMs = 500)
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Quns {
    [DllImport("shell32.dll")] public static extern int SHQueryUserNotificationState(out int s);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
}
"@
$names = @{1="NOT_PRESENT";2="BUSY";3="RUNNING_D3D_FULL_SCREEN";4="PRESENTATION_MODE";5="ACCEPTS_NOTIFICATIONS";6="QUIET_TIME";7="APP"}
for ($i = 0; $i -lt $Count; $i++) {
    $s = 0
    [Quns]::SHQueryUserNotificationState([ref]$s) | Out-Null
    $h = [Quns]::GetForegroundWindow()
    $c = New-Object System.Text.StringBuilder 128; [Quns]::GetClassName($h, $c, 128) | Out-Null
    $t = New-Object System.Text.StringBuilder 128; [Quns]::GetWindowText($h, $t, 128) | Out-Null
    $r = New-Object Quns+RECT; [Quns]::GetWindowRect($h, [ref]$r) | Out-Null
    $pet = [Quns]::FindWindow("PetOverlayWindow", $null)
    $petVis = if ($pet -eq [IntPtr]::Zero) { "none" } elseif ([Quns]::IsWindowVisible($pet)) { "visible" } else { "hidden" }
    Write-Output ("{0:HH:mm:ss.fff} QUNS={1} ({2}) pet={3} foreground={4} ""{5}"" rect=({6},{7},{8},{9})" -f (Get-Date), $s, $names[$s], $petVis, $c.ToString(), $t.ToString(), $r.l, $r.t, $r.r, $r.b)
    if ($i + 1 -lt $Count) { Start-Sleep -Milliseconds $IntervalMs }
}
