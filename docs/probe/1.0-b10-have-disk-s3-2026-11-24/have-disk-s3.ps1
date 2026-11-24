param(
    [string]$Target = '127.0.0.1:9879',
    [string]$Out = 'C:\Users\michael\AppData\Local\Temp\claude\d0f9ac92ab0a27f1\01813cd9-7995-4858-a9d3-b5c9055c615e\scratchpad\b10\run2',
    [string]$Package = 'C:\Users\michael\AppData\Local\Temp\claude\d0f9ac92ab0a27f1\01813cd9-7995-4858-a9d3-b5c9055c615e\scratchpad\b10\s3-060'
)
# The released s3 package by Have Disk on the S3 reference clone, the clicks
# of the first run, with the registry read after Finish and before Yes.
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
$null = & $c -Target $Target push-tree -Source $Package -Destination 'C:\V9X060S' -Json
$h = 'HKLM\Software\Microsoft\Windows\CurrentVersion\Controls Folder\Display\shellex\PropertySheetHandlers\Velocity9x'
Reg 'handler before' $h
Reg 'RunOnce before' 'HKLM\Software\Microsoft\Windows\CurrentVersion\RunOnce'
$null = & $c -Target $Target exec -Application 'C:\WINDOWS\CONTROL.EXE' -Arguments 'sysdm.cpl,,1' -Detach -Json
$null = & $c -Target $Target wait-window -Title 'System Properties' -WaitSeconds 30 -Json
Step '01-expand' 'move 149,258; click left' 800
Step '02-props' 'move 276,274; doubleclick left'
Step '03-driver' 'move 219,217; click left' 800
Step '04-update' 'move 424,538; click left' 2000
Step '05-next' 'move 472,542; click left'
Step '06-list' 'move 327,366; click left; delay 300; move 472,542; click left' 2500
Step '07-havedisk' 'move 545,480; click left'
Step '08-path' 'type C:\V9X060S; delay 200; move 530,343; click left' 3000
Step '09-model-ok' 'move 509,595; click left' 2500
Step '10-install' 'move 472,542; click left' 8000
Reg 'handler after copy' $h
Reg 'RunOnce after copy' 'HKLM\Software\Microsoft\Windows\CurrentVersion\RunOnce'
Step '11-finish' 'move 472,542; click left' 3000
Reg 'handler after Finish' $h
Reg 'RunOnce after Finish' 'HKLM\Software\Microsoft\Windows\CurrentVersion\RunOnce'
