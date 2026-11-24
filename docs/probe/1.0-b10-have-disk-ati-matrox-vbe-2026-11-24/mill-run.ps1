param(
    [Parameter(Mandatory)][string]$Out,
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$GuestFolder
)
# Have Disk with the matrox package on a fresh Millennium clone, the click
# path measured on Win98SE-B10-Millennium (System Properties at its s3
# position, the adapter row at 254,273), then the restart, then a poll of
# the handler Tag, the moved copy and V9XSYNC.INI without opening Display
# Properties.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
New-Item -ItemType Directory -Force $Out | Out-Null
function Step([string]$name, [string]$sequence, [int]$settle) {
    & $c -Port 9879 input -Sequence $sequence -OutFile (Join-Path $Out "$name.png") -SettleMs $settle *> $null
}
function TagText {
    $h = 'HKLM\Software\Microsoft\Windows\CurrentVersion\Controls Folder\Display\shellex\PropertySheetHandlers\Velocity9x'
    $k = & $c -Port 9879 reg -Key $h -Json 2>&1 | Out-String
    if ($k -match '"Tag","Type":"REG_DWORD","Data":(\d+)') { return ('0x{0:X8}' -f [uint32]$Matches[1]) }
    return 'none'
}
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt 360) {
    $r = & $c -Port 9879 info -Json 2>$null | Out-String
    if ($r -match '"DesktopReady":true') { break }
    Start-Sleep 5
}
Start-Sleep 20
"clean: V9XDISP.DRV " + ((& $c -Port 9879 stat -Path 'C:\WINDOWS\SYSTEM\V9XDISP.DRV' -Json | Out-String) -match '"Exists":true') +
    ", V9XDIAG " + ((& $c -Port 9879 stat -Path 'C:\V9XDIAG' -Json | Out-String) -match '"Exists":true')
$null = & $c -Port 9879 push-tree -Source $Package -Destination $GuestFolder -Json
$null = & $c -Port 9879 exec -Application 'C:\WINDOWS\CONTROL.EXE' -Arguments 'sysdm.cpl,,1' -Detach -Json
$null = & $c -Port 9879 wait-window -Title 'System Properties' -WaitSeconds 30 -Json
Step '01-expand' 'move 149,258; click left' 1000
Step '02-props' 'move 254,273; doubleclick left' 2000
Step '03-driver' 'move 219,217; click left' 1000
Step '04-update' 'move 424,538; click left' 2500
Step '05-next' 'move 472,542; click left' 1500
Step '06-list' 'move 327,366; click left; delay 300; move 472,542; click left' 3000
Step '07-havedisk' 'move 545,480; click left' 1500
Step '08-path' "type $GuestFolder; delay 200; move 530,343; click left" 3500
Step '09-model-ok' 'move 509,595; click left' 2500
Step '10-install' 'move 472,542; click left' 10000
"after copy Tag=" + (TagText)
Step '11-finish' 'move 472,542; click left' 3000
"before restart: " + ((& $c -Port 9879 reg -Key 'HKCC\Display\Settings' -Json | Out-String) -replace '.*?"BitsPerPixel","Type":"REG_SZ","Data":"(\d+)".*?"Resolution","Type":"REG_SZ","Data":"([\d,]+)".*', 'BPP=$1 Res=$2').Trim()
& $c -Port 9879 input -Sequence 'move 475,428; click left' -SettleMs 500 *> $null
$t0 = Get-Date
$seenDown = $false
while (((Get-Date) - $t0).TotalSeconds -lt 300) {
    $r = & $c -Port 9879 info -Json 2>$null | Out-String
    if ($r -notmatch 'ComputerName') { $seenDown = $true } elseif ($seenDown) { break }
    Start-Sleep 2
}
for ($i = 0; $i -lt 30; $i++) {
    $sy = (& $c -Port 9879 stat -Path 'C:\V9XDIAG\V9XSYNC.INI' -Json 2>$null | Out-String) -match '"Exists":true'
    $d = & $c -Port 9879 reg -Key 'HKLM\System\CurrentControlSet\Services\Class\Display\0000\shellex\PropertySheetHandlers' -Json 2>&1 | Out-String
    $dev = if ($d -match '"Velocity9x"') { 'present' } else { 'absent' }
    "$([int]((Get-Date) - $t0).TotalSeconds)s Tag=$(TagText) sync=$sy devicecopy=$dev"
    Start-Sleep 3
}
foreach ($f in 'V9XBOOT.INI', 'V9XHW.INI', 'V9XSYNC.INI') {
    & $c -Port 9879 get -Source "C:\V9XDIAG\$f" -Destination (Join-Path $Out $f) -Json *> $null
}
$null = & $c -Port 9879 exec -Application 'C:\WINDOWS\CONTROL.EXE' -Arguments 'desk.cpl' -Detach -Json
$null = & $c -Port 9879 wait-window -Title 'Display Properties' -WaitSeconds 30 -Json
& $c -Port 9879 screenshot -OutFile (Join-Path $Out '12-display-properties.png') -Json *> $null
"after Display Properties Tag=" + (TagText)
