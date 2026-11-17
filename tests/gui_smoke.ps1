param([string]$Exe = "$PSScriptRoot\..\build\RestCue.exe")
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class RestCueSmoke {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr param);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] private static extern int MapWindowPoints(IntPtr from, IntPtr to, ref Rect rect, uint points);
    private static Rect[] ControlRects(IntPtr parent, int[] ids) {
        var result = new Rect[ids.Length];
        for (int i = 0; i < ids.Length; i++) {
            var child = GetDlgItem(parent, ids[i]);
            if (child == IntPtr.Zero || !GetWindowRect(child, out result[i])) return null;
            MapWindowPoints(IntPtr.Zero, parent, ref result[i], 2);
        }
        return result;
    }
    public static bool ControlsFit(IntPtr parent, int[] ids) {
        var controls = ControlRects(parent, ids);
        Rect client;
        if (controls == null || !GetClientRect(parent, out client)) return false;
        foreach (var r in controls)
            if (r.Left < 0 || r.Top < 0 || r.Right > client.Right || r.Bottom > client.Bottom) return false;
        return true;
    }
    public static bool NoOverlaps(IntPtr parent, int[] ids) {
        var controls = ControlRects(parent, ids);
        if (controls == null) return false;
        for (int i = 0; i < controls.Length; i++)
            for (int j = i + 1; j < controls.Length; j++)
                if (Math.Max(controls[i].Left, controls[j].Left) < Math.Min(controls[i].Right, controls[j].Right) &&
                    Math.Max(controls[i].Top, controls[j].Top) < Math.Min(controls[i].Bottom, controls[j].Bottom)) return false;
        return true;
    }
    public static bool SteppersBalanced(IntPtr parent) {
        var rects = ControlRects(parent, new int[] {101, 211, 212});
        if (rects == null) return false;
        var input = rects[0]; var up = rects[1]; var down = rects[2];
        double scale = GetDpiForWindow(parent) / 96.0 * 0.78;
        int tolerance = (int)Math.Ceiling(4 * scale);
        return Math.Abs(up.Top + up.Bottom + down.Top + down.Bottom - 2 * (input.Top + input.Bottom)) <= 4 &&
               up.Top >= input.Top - tolerance && down.Bottom <= input.Bottom + tolerance &&
               down.Top - up.Bottom >= Math.Max(2, (int)Math.Round(3 * scale)) &&
               up.Bottom - up.Top == down.Bottom - down.Top;
    }
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    public static void ClickFormerButtonArea(IntPtr hwnd) {
        Rect rect; GetClientRect(hwnd, out rect);
        double scale = Math.Min(rect.Right / 1024.0, rect.Bottom / 640.0);
        int x = rect.Right / 2, y = (int)(rect.Bottom / 2.0 + 210 * scale);
        SendMessage(hwnd, 0x202, IntPtr.Zero, (IntPtr)((y << 16) | x));
    }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr SetTextMessage(IntPtr hwnd, uint msg, IntPtr wp, string text);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr GetTextMessage(IntPtr hwnd, uint msg, IntPtr wp, StringBuilder text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int cmd);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr param);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool GetLayeredWindowAttributes(IntPtr hwnd, out uint color, out byte alpha, out uint flags);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr hwnd, int index);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileInt(string section, string key, int fallback, string path);
    public static IntPtr MainWindow() { return FindWindow("RestCue.MainWindow", null); }
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
    public static IntPtr[] Overlays() { return WindowsOfClass("RestCue.Overlay"); }
    public static IntPtr[] RestCards() { return WindowsOfClass("RestCue.RestCard"); }
    private static IntPtr[] WindowsOfClass(string name) {
        var windows = new List<IntPtr>();
        EnumWindows((hwnd, param) => {
            var cls = new StringBuilder(100);
            GetClassName(hwnd, cls, cls.Capacity);
            if (cls.ToString() == name && IsWindowVisible(hwnd)) windows.Add(hwnd);
            return true;
        }, IntPtr.Zero);
        return windows.ToArray();
    }
    public static bool CardsCentered() {
        var cards = RestCards();
        if (cards.Length == 0) return false;
        foreach (var card in cards) {
            Rect pane, shade;
            var owner = GetWindow(card, 4);
            if (owner == IntPtr.Zero || !GetWindowRect(card, out pane) || !GetWindowRect(owner, out shade)) return false;
            var paneW = pane.Right - pane.Left;
            var paneH = pane.Bottom - pane.Top;
            var shadeW = shade.Right - shade.Left;
            var shadeH = shade.Bottom - shade.Top;
            if (Math.Abs(pane.Left + pane.Right - shade.Left - shade.Right) > 4) return false;
            // The circle is square at the top; the instruction extends beneath it.
            if (Math.Abs(pane.Top * 2 + paneW - shade.Top - shade.Bottom) > 4) return false;
            if (paneW >= shadeW || paneH >= shadeH || Math.Abs(paneH * 440.0 / 500 - paneW) > 4) return false;
        }
        return true;
    }
    public static string Text(IntPtr hwnd) {
        var text = new StringBuilder(512);
        GetTextMessage(hwnd, 0xD, (IntPtr)text.Capacity, text);
        return text.ToString();
    }
}
'@

[void][RestCueSmoke]::SetThreadDpiAwarenessContext([IntPtr](-4))
if ([RestCueSmoke]::MainWindow() -ne [IntPtr]::Zero) {
    throw 'Close RestCue before running the GUI smoke test.'
}
$Exe = (Resolve-Path $Exe).Path
$config = Join-Path (Split-Path $Exe) 'RestCue.ini'
$userConfig = Join-Path $env:LOCALAPPDATA 'RestCue\settings.ini'
$backups = foreach ($path in @($config, $userConfig)) {
    if (Test-Path $path) {
        $file = Get-Item $path
        @{ Path=$path; Data=[IO.File]::ReadAllBytes($path); Modified=$file.LastWriteTimeUtc }
    } else {
        @{ Path=$path; Data=$null; Modified=$null }
    }
}
$process = $null
$checks = 0
function Check($condition, [string]$description) {
    if (-not $condition) { throw "FAILED: $description" }
    $script:checks++
    Write-Output "PASS: $description"
}
function Cmd([int]$id) {
    [void][RestCueSmoke]::SendMessage($script:main, 0x111, [IntPtr]$id, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 120
}
function Set-Field([int]$id, [string]$value) {
    $field = [RestCueSmoke]::GetDlgItem($script:main, $id)
    [void][RestCueSmoke]::SetTextMessage($field, 0xC, [IntPtr]::Zero, $value)
    if ([RestCueSmoke]::Text($field) -ne $value) { throw "Cannot set field $id" }
}
function Status-Text { [RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($script:main, 301)) }
function Overlay-Count { @([RestCueSmoke]::Overlays()).Count }
function Card-Count { @([RestCueSmoke]::RestCards()).Count }
function Wait-Visible([IntPtr]$window) {
    # A window handle and input-idle notification can precede ShowWindow during DWM setup.
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ([RestCueSmoke]::IsWindowVisible($window)) { return $true }
        if ($script:process.HasExited) { return $false }
        Start-Sleep -Milliseconds 50
    }
    return $false
}

try {
    $process = Start-Process -FilePath $Exe -PassThru
    $main = [IntPtr]::Zero
    for ($i = 0; $i -lt 50; $i++) {
        Start-Sleep -Milliseconds 100
        $main = [RestCueSmoke]::MainWindow()
        if ($main -ne [IntPtr]::Zero) { break }
    }
    Check ($main -ne [IntPtr]::Zero) 'main window opens'
    $process.WaitForInputIdle(5000) | Out-Null
    Check (Wait-Visible $main) 'settings window is visible'
    $client = New-Object RestCueSmoke+Rect
    [void][RestCueSmoke]::GetClientRect($main, [ref]$client)
    $dpi = [RestCueSmoke]::GetDpiForWindow($main)
    Check ($client.Right -eq [Math]::Round(800 * 0.78 * $dpi / 96, [MidpointRounding]::AwayFromZero)) 'settings window uses compact width at current DPI'
    Check ($client.Bottom -eq [Math]::Round(532 * 0.78 * $dpi / 96, [MidpointRounding]::AwayFromZero)) 'settings window uses compact height at current DPI'
    Check ([RestCueSmoke]::GetDlgItem($main, 205) -eq [IntPtr]::Zero) 'footer tray button is removed'
    $controls = [int[]]@(101,102,103,104,105,201,202,203,204,206,207,208,211,212)
    Check ([RestCueSmoke]::ControlsFit($main, $controls)) 'all controls fit the compact window'
    Check ([RestCueSmoke]::NoOverlaps($main, $controls)) 'setting inputs and buttons do not overlap'
    Check ([RestCueSmoke]::SteppersBalanced($main)) 'minute arrows are inset and vertically balanced'
    Set-Field 101 '1'
    Set-Field 102 '0'
    Set-Field 103 '4'
    Set-Field 104 '60'
    Cmd 211
    Check ([RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($main, 101)) -eq '2') 'work stepper increments the interval'
    Cmd 212
    Check ([RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($main, 101)) -eq '1') 'work stepper decrements the interval'
    $slider = [RestCueSmoke]::GetDlgItem($main, 105)
    Check ($slider -ne [IntPtr]::Zero) 'opacity slider is present'
    [void][RestCueSmoke]::SendMessage($slider, 0x100, [IntPtr]39, [IntPtr]::Zero)
    Check ([RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($main, 104)) -eq '61') 'slider keyboard input updates the percentage'
    [void][RestCueSmoke]::SendMessage($slider, 0x100, [IntPtr]36, [IntPtr]::Zero)
    Check ([RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($main, 104)) -eq '10') 'slider respects the minimum'
    [void][RestCueSmoke]::SendMessage($slider, 0x100, [IntPtr]35, [IntPtr]::Zero)
    Check ([RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($main, 104)) -eq '95') 'slider respects the maximum'
    $sliderRect = New-Object RestCueSmoke+Rect
    [void][RestCueSmoke]::GetClientRect($slider, [ref]$sliderRect)
    $point = [IntPtr](([int]($sliderRect.Bottom / 2) -shl 16) -bor [int]($sliderRect.Right / 2))
    [void][RestCueSmoke]::SendMessage($slider, 0x201, [IntPtr]1, $point)
    [void][RestCueSmoke]::SendMessage($slider, 0x202, [IntPtr]::Zero, $point)
    $dragged = [int][RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($main, 104))
    Check ($dragged -ge 51 -and $dragged -le 54) 'slider mouse input updates the percentage'
    Set-Field 104 '60'
    Cmd 201
    Check ([RestCueSmoke]::GetPrivateProfileInt('Timer', 'WorkMinutes', -1, $config) -eq 1) 'work interval is saved'
    Check ([RestCueSmoke]::GetPrivateProfileInt('Timer', 'RestSeconds', -1, $config) -eq 4) 'break duration is saved'
    Check ([RestCueSmoke]::GetPrivateProfileInt('Timer', 'Opacity', -1, $config) -eq 60) 'opacity is saved'
    Check ([RestCueSmoke]::GetPrivateProfileInt('Timer', 'LaunchAtStartup', -1, $config) -eq 0) 'startup stays off unless enabled'
    # Repeated saves must not warn, and a new process must restore every field.
    Cmd 201
    Cmd 303
    Check ($process.WaitForExit(5000)) 'exit after saving configuration'
    $process = Start-Process -FilePath $Exe -PassThru
    $main = [IntPtr]::Zero
    for ($i = 0; $i -lt 50; $i++) {
        Start-Sleep -Milliseconds 100
        $main = [RestCueSmoke]::MainWindow()
        if ($main -ne [IntPtr]::Zero) { break }
    }
    Check ($main -ne [IntPtr]::Zero) 'restart opens the settings window'
    $process.WaitForInputIdle(5000) | Out-Null
    if (-not (Wait-Visible $main)) { throw 'Restarted settings window did not become visible within 5 seconds' }
    Check ([RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($main, 101)) -eq '1') 'restart restores the work interval'
    Check ([RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($main, 102)) -eq '0' -and
           [RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($main, 103)) -eq '4') 'restart restores the break duration'
    Check ([RestCueSmoke]::Text([RestCueSmoke]::GetDlgItem($main, 104)) -eq '60') 'restart restores opacity'
    Cmd 202
    $frozen = Status-Text
    Start-Sleep -Milliseconds 1100
    Check ((Status-Text) -eq $frozen) 'pause freezes the countdown'
    Cmd 202
    Check ((Status-Text) -ne $frozen) 'resume leaves the paused state'

    $monitors = [RestCueSmoke]::GetSystemMetrics(80)
    Cmd 203
    Check ((Overlay-Count) -eq $monitors) 'manual break covers every monitor'
    Check ((Card-Count) -eq $monitors) 'every monitor has a separate rest card'
    Check (-not [RestCueSmoke]::IsWindowVisible($main)) 'preview temporarily collects settings into the tray'
    Check ([RestCueSmoke]::CardsCentered()) 'rest clock is compact and centered on each monitor'
    Start-Sleep -Milliseconds 650
    $windows = @([RestCueSmoke]::Overlays())
    [uint32]$color = 0; [byte]$alpha = 0; [uint32]$flags = 0
    [void][RestCueSmoke]::GetLayeredWindowAttributes($windows[0], [ref]$color, [ref]$alpha, [ref]$flags)
    Check ($alpha -gt 0 -and $alpha -le 153) 'overlay fades to the selected opacity'
    Start-Sleep -Milliseconds 900
    $card = @([RestCueSmoke]::RestCards())[0]
    [byte]$cardAlpha = 0
    $cardAttributes = [RestCueSmoke]::GetLayeredWindowAttributes($card, [ref]$color, [ref]$cardAlpha, [ref]$flags)
    Check (-not $cardAttributes -and (([RestCueSmoke]::GetWindowLong($card, -20) -band 0x80000) -ne 0)) 'rest cue uses per-pixel alpha rather than an opaque card'
    [void][RestCueSmoke]::SendMessage($card, 0x100, [IntPtr]27, [IntPtr]::Zero)
    [void][RestCueSmoke]::SendMessage($card, 0x202, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    Check ((Card-Count) -eq $monitors) 'rest card ignores Esc and left click'
    [void][RestCueSmoke]::SendMessage($windows[0], 0x100, [IntPtr]27, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    Check ((Overlay-Count) -eq $monitors) 'Esc no longer dismisses the break'
    [void][RestCueSmoke]::SendMessage($windows[0], 0x202, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    Check ((Overlay-Count) -eq $monitors) 'left click does not dismiss the break'
    [void][RestCueSmoke]::SendMessage($windows[0], 0x205, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    Check ((Overlay-Count) -eq 0 -and (Card-Count) -eq 0) 'right click dismisses shade and rest card'
    Check ([RestCueSmoke]::IsWindowVisible($main)) 'exiting the preview restores the settings window'

    Cmd 203
    Cmd 202
    Check ((Overlay-Count) -eq 0 -and (Card-Count) -eq 0) 'pausing a break hides both rest layers'
    Check ([RestCueSmoke]::IsWindowVisible($main)) 'pausing preview restores settings'
    Cmd 202
    Check ((Overlay-Count) -eq $monitors -and (Card-Count) -eq $monitors) 'resuming a break restores both layers'
    $overlay = @([RestCueSmoke]::RestCards())[0]
    [void][RestCueSmoke]::SendMessage($overlay, 0x205, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    Check ((Overlay-Count) -eq 0 -and (Card-Count) -eq 0) 'right click on the card dismisses the resumed break'
    Cmd 203
    [void][RestCueSmoke]::SendMessage($main, 0x312, [IntPtr]1, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    Check ((Overlay-Count) -eq $monitors) 'old global hotkey message no longer skips the break'
    Cmd 204
    Cmd 203
    [RestCueSmoke]::ClickFormerButtonArea(@([RestCueSmoke]::Overlays())[0])
    Start-Sleep -Milliseconds 150
    Check ((Overlay-Count) -eq $monitors) 'removed button area has no invisible left-click hotspot'
    Cmd 204
    Cmd 203
    Start-Sleep -Milliseconds 4200
    Check ((Overlay-Count) -eq 0 -and (Card-Count) -eq 0) 'both rest layers end automatically'
    Check ([RestCueSmoke]::IsWindowVisible($main)) 'automatic preview completion restores settings'
    Check ((Status-Text) -match '0[01]:[0-5][0-9]') 'working countdown restarts after the break'

    [void][RestCueSmoke]::SendMessage($main, 0x112, [IntPtr]0xF020, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    Check (-not [RestCueSmoke]::IsWindowVisible($main)) 'minimize collects settings into the tray'
    [void][RestCueSmoke]::SendMessage($main, 0x8001, [IntPtr]1, [IntPtr]0x202)
    Check ([RestCueSmoke]::IsWindowVisible($main)) 'tray click callback restores settings'
    [void][RestCueSmoke]::SendMessage($main, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    Check (-not [RestCueSmoke]::IsWindowVisible($main) -and -not $process.HasExited) 'close button hides to tray without exiting'
    Cmd 203
    Check ((Overlay-Count) -eq $monitors -and (Card-Count) -eq $monitors) 'break works while settings are collected in the tray'
    Cmd 204
    Check (-not [RestCueSmoke]::IsWindowVisible($main)) 'background break does not reopen hidden settings'
    $second = Start-Process -FilePath $Exe -PassThru
    Check ($second.WaitForExit(5000)) 'second launch exits rather than duplicating the app'
    Check ([RestCueSmoke]::IsWindowVisible($main)) 'second launch reopens settings'
    [void][RestCueSmoke]::ShowWindow($main, 6)
    Start-Sleep -Milliseconds 150
    Cmd 203
    Check ((Overlay-Count) -eq $monitors) 'break works while settings are minimized'
    Cmd 204
    Cmd 304
    $stopped = Status-Text
    Start-Sleep -Milliseconds 1100
    Check ((Status-Text) -eq $stopped) 'stop leaves the scheduler stopped'
    Cmd 303
    Check ($process.WaitForExit(5000)) 'tray exit command shuts down cleanly'
    Check ($process.ExitCode -eq 0) 'process exits successfully'
    Write-Output "$checks GUI checks passed"
} finally {
    if ($process -and -not $process.HasExited) {
        [void][RestCueSmoke]::PostMessage($main, 0x111, [IntPtr]303, [IntPtr]::Zero)
        if (-not $process.WaitForExit(2000)) { $process.Kill(); $process.WaitForExit() }
    }
    foreach ($backup in $backups) {
        if ($null -ne $backup.Data) {
            [IO.File]::WriteAllBytes($backup.Path, $backup.Data)
            [IO.File]::SetLastWriteTimeUtc($backup.Path, $backup.Modified)
        } elseif (Test-Path $backup.Path) {
            Remove-Item $backup.Path
        }
    }
}
