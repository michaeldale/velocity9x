param([Parameter(Mandatory)][int]$ProcessId, [Parameter(Mandatory)][string]$OutFile)
# PrintWindow capture of one 86Box process's main window, chosen by pid.
Add-Type -AssemblyName System.Drawing
if (-not ('V9xPw2' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class V9xPw2 {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
}
$p = Get-Process -Id $ProcessId
$h = $p.MainWindowHandle
$r = New-Object V9xPw2+RECT
[V9xPw2]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[V9xPw2]::PrintWindow($h, $dc, 2) | Out-Null
$g.ReleaseHdc($dc); $g.Dispose()
$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
"$($p.MainWindowTitle) ${w}x$ht"
