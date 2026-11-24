param(
    [string]$Target = '127.0.0.1:9879',
    [Parameter(Mandatory)][string]$Out,
    [Parameter(Mandatory)][string]$Package,      # host folder with the package files
    [Parameter(Mandatory)][string]$GuestFolder,  # e.g. C:\V9XATI
    [ValidateSet('open', 'install', 'finish')][string]$Phase
)
# Have Disk on a fresh B10 clone, in three phases so each screen can be
# read before the next click: open (to the model list), install (OK on
# the selected model, Next to copy), finish (Finish; the restart prompt is
# left for the caller). Coordinates are the s3 run's (have-disk-s3.ps1).
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
New-Item -ItemType Directory -Force $Out | Out-Null
function Step([string]$name, [string]$sequence, [int]$settle = 1500) {
    & $c -Target $Target input -Sequence $sequence -OutFile (Join-Path $Out "$name.png") -SettleMs $settle *> $null
}
function Reg([string]$label, [string]$key) {
    $r = & $c -Target $Target reg -Key $key -Json 2>&1 | Out-String
    "== $label"
    $r.Trim()
}
$h = 'HKLM\Software\Microsoft\Windows\CurrentVersion\Controls Folder\Display\shellex\PropertySheetHandlers\Velocity9x'
switch ($Phase) {
    'open' {
        $null = & $c -Target $Target push-tree -Source $Package -Destination $GuestFolder -Json
        Reg 'handler before' $h
        $null = & $c -Target $Target exec -Application 'C:\WINDOWS\CONTROL.EXE' -Arguments 'sysdm.cpl,,1' -Detach -Json
        $null = & $c -Target $Target wait-window -Title 'System Properties' -WaitSeconds 30 -Json
        Step '01-expand' 'move 149,258; click left' 800
        Step '02-props' 'move 276,274; doubleclick left'
        Step '03-driver' 'move 219,217; click left' 800
        Step '04-update' 'move 424,538; click left' 2000
        Step '05-next' 'move 472,542; click left'
        Step '06-list' 'move 327,366; click left; delay 300; move 472,542; click left' 2500
        Step '07-havedisk' 'move 545,480; click left'
        Step '08-path' "type $GuestFolder; delay 200; move 530,343; click left" 3000
    }
    'install' {
        Step '09-model-ok' 'move 509,595; click left' 2500
        Step '10-install' 'move 472,542; click left' 8000
        Reg 'handler after copy' $h
    }
    'finish' {
        Step '11-finish' 'move 472,542; click left' 3000
        Reg 'handler after Finish' $h
        Reg 'RunOnce after Finish' 'HKLM\Software\Microsoft\Windows\CurrentVersion\RunOnce'
        Reg 'display settings after Finish' 'HKCC\Display\Settings'
    }
}
