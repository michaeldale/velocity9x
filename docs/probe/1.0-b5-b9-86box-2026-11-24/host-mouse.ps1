param(
    [Parameter(Mandatory)][int]$ProcessId,
    [ValidateSet('capture', 'move', 'click', 'release')][string]$Action,
    [int]$X, [int]$Y,      # capture: window-relative point to click to take the mouse
    [int]$Dx, [int]$Dy     # move: guest pixels; sent halved in small steps (86Box doubles)
)
# Host-side mouse into an 86Box window, for dialogs that come before the agent.
if (-not ('V9xMouse' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class V9xMouse {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
}
$h = (Get-Process -Id $ProcessId).MainWindowHandle
[V9xMouse]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 300
switch ($Action) {
    'capture' {
        $r = New-Object V9xMouse+RECT
        [V9xMouse]::GetWindowRect($h, [ref]$r) | Out-Null
        [V9xMouse]::SetCursorPos($r.Left + $X, $r.Top + $Y) | Out-Null
        Start-Sleep -Milliseconds 200
        [V9xMouse]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        [V9xMouse]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    }
    'move' {
        $hx = [int]($Dx / 2); $hy = [int]($Dy / 2)
        $steps = [Math]::Max(1, [int]([Math]::Ceiling([Math]::Max([Math]::Abs($hx), [Math]::Abs($hy)) / 8)))
        for ($i = 0; $i -lt $steps; $i++) {
            $sx = [int]($hx * ($i + 1) / $steps) - [int]($hx * $i / $steps)
            $sy = [int]($hy * ($i + 1) / $steps) - [int]($hy * $i / $steps)
            [V9xMouse]::mouse_event(0x0001, $sx, $sy, 0, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 40
        }
    }
    'click' {
        [V9xMouse]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 80
        [V9xMouse]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    }
    'release' {
        [V9xMouse]::mouse_event(0x0020, 0, 0, 0, [UIntPtr]::Zero)
        [V9xMouse]::mouse_event(0x0040, 0, 0, 0, [UIntPtr]::Zero)
    }
}
