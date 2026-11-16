param([string]$Exe = "$PSScriptRoot\..\build\RestCue.exe")
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RestCueIconCheck {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll")]
    public static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")]
    public static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr module);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr LoadImage(IntPtr module, IntPtr name, uint type, int width, int height, uint flags);
    [DllImport("user32.dll")]
    public static extern bool DestroyIcon(IntPtr icon);
}
'@
$Exe = (Resolve-Path $Exe).Path
$module = [RestCueIconCheck]::LoadLibraryEx($Exe, [IntPtr]::Zero, 0x22)
if ($module -eq [IntPtr]::Zero) { throw 'Cannot open EXE resources' }
try {
    $group = [RestCueIconCheck]::FindResource($module, [IntPtr]101, [IntPtr]14)
    if ($group -eq [IntPtr]::Zero) { throw 'Custom icon group 101 is missing' }
    $data = [RestCueIconCheck]::LockResource([RestCueIconCheck]::LoadResource($module, $group))
    if ($data -eq [IntPtr]::Zero) { throw 'Icon group could not be read' }
    $count = [Runtime.InteropServices.Marshal]::ReadInt16($data, 4)
    if ($count -ne 7) { throw "Expected 7 icon resolutions, found $count" }
    foreach ($size in @(16, 24, 32, 48, 64, 128, 256)) {
        $icon = [RestCueIconCheck]::LoadImage($module, [IntPtr]101, 1, $size, $size, 0)
        if ($icon -eq [IntPtr]::Zero) { throw "Cannot load embedded $size pixel icon" }
        [void][RestCueIconCheck]::DestroyIcon($icon)
        Write-Output "PASS: embedded $size x $size icon loads"
    }
    Write-Output 'PASS: EXE contains the complete custom icon group'
} finally {
    [void][RestCueIconCheck]::FreeLibrary($module)
}
