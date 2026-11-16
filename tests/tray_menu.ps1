param([string]$Exe = "$PSScriptRoot\..\RestCue.exe")
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class RestCueMenuCheck {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr parameter);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr menu, int position);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuString(IntPtr menu, uint item, StringBuilder text, int count, uint flags);
    public static IntPtr MainWindow() { return FindWindow("RestCue.MainWindow", null); }
    public static string RestCaption() { return new string(new char[] { (char)0x7acb, (char)0x5373, (char)0x4f11, (char)0x606f }); }
    public static IntPtr OwnedMenu(uint expectedPid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, parameter) => {
            uint pid; GetWindowThreadProcessId(hwnd, out pid);
            if (pid != expectedPid || !IsWindowVisible(hwnd)) return true;
            var cls = new StringBuilder(80);
            GetClassName(hwnd, cls, cls.Capacity);
            if (cls.ToString() == "#32768") { found = hwnd; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
$Exe = (Resolve-Path $Exe).Path
$main = [RestCueMenuCheck]::MainWindow()
if ($main -eq [IntPtr]::Zero) {
    $started = Start-Process -FilePath $Exe -PassThru
    $started.WaitForInputIdle(5000) | Out-Null
    for ($i=0; $i -lt 100; $i++) {
        $main = [RestCueMenuCheck]::MainWindow()
        if ($main -ne [IntPtr]::Zero -and [RestCueMenuCheck]::IsWindowVisible($main)) { break }
        Start-Sleep -Milliseconds 50
    }
}
if ($main -eq [IntPtr]::Zero) { throw 'RestCue window missing' }
[uint32]$appPid = 0
[void][RestCueMenuCheck]::GetWindowThreadProcessId($main, [ref]$appPid)
if ((Get-Process -Id $appPid).Path -ne $Exe) { throw 'Unexpected app path' }
try {
    [void][RestCueMenuCheck]::PostMessage($main, 0x8001, [IntPtr]1, [IntPtr]0x205)
    $menuWindow = [IntPtr]::Zero
    for ($i=0; $i -lt 100; $i++) {
        $menuWindow = [RestCueMenuCheck]::OwnedMenu($appPid)
        if ($menuWindow -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 30
    }
    if ($menuWindow -eq [IntPtr]::Zero) { throw 'Owned tray menu did not open' }
    $menu = [RestCueMenuCheck]::SendMessage($menuWindow, 0x1e1, [IntPtr]::Zero, [IntPtr]::Zero)
    $count = [RestCueMenuCheck]::GetMenuItemCount($menu)
    if ($count -ne 6) { throw "Expected 5 actions and 1 separator, found $count items" }
    $expected = [uint32[]]@(302,202,203,204,0,303)
    for ($i=0; $i -lt $count; $i++) {
        $id = [RestCueMenuCheck]::GetMenuItemID($menu, $i)
        if ($id -ne $expected[$i]) { throw "Unexpected menu item ID $id at $i" }
    }
    Write-Output 'PASS: native tray menu has the expected actions and no Stop item'
    $label = New-Object Text.StringBuilder 128
    [void][RestCueMenuCheck]::GetMenuString($menu, 203, $label, $label.Capacity, 0)
    if ($label.ToString() -ne [RestCueMenuCheck]::RestCaption()) { throw 'Rest caption still contains extra explanatory text' }
    Write-Output 'PASS: rest menu caption has no saved-settings suffix'
} finally {
    [void][RestCueMenuCheck]::PostMessage($main, 0x1f, [IntPtr]::Zero, [IntPtr]::Zero)
}
for ($i=0; $i -lt 50 -and [RestCueMenuCheck]::OwnedMenu($appPid) -ne [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 30
}
if ([RestCueMenuCheck]::OwnedMenu($appPid) -ne [IntPtr]::Zero) { throw 'Tray menu did not close after inspection' }
Write-Output 'PASS: tray menu closes without choosing an action'
Write-Output "RestCue remains running, PID=$appPid"
