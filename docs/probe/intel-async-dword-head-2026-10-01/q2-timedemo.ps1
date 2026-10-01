param([string]$Tag, [int]$Seconds = 150, [string]$HostName = '10.0.1.254', [string]$Job = 'HLPROF')
# Quake 2 demo on the netbook: the attract loop replays demo2 under
# timedemo 1; each pass prints frames/seconds/fps to baseq2\qconsole.log.
# config.cfg is saved first and put back after, because Quake 2 writes its
# +set cvars there on quit. Snapshots either side.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$dir = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\d3b4cbce-9a5f-4536-a3b0-18f7005d7768\scratchpad'
$trace = "C:\V9XREMOTE\JOBS\$Job\V9XTRACE.EXE"
function Snap([string]$name) {
    & $ctl exec -EndpointHost $HostName -Application $trace -WorkingDirectory "C:\V9XREMOTE\JOBS\$Job" -Detach | Out-Null
    Start-Sleep 5
    & $ctl get -EndpointHost $HostName -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $dir $name) | Out-Null
}
$cfg = Join-Path $dir "$Tag-config.cfg"
$r = & $ctl get -EndpointHost $HostName -Source 'C:\Q2Demo\baseq2\config.cfg' -Destination $cfg -Json | ConvertFrom-Json
if (-not $r.Success) { throw 'could not save config.cfg' }
Snap "$Tag-pre.ini"
& $ctl exec -EndpointHost $HostName -Application 'C:\Q2Demo\quake2.exe' -Arguments '+set vid_ref gl +set logfile 2 +set timedemo 1' -WorkingDirectory 'C:\Q2Demo' -Detach | Out-Null
Start-Sleep $Seconds
Snap "$Tag-post.ini"
& $ctl input -EndpointHost $HostName -Sequence 'type `; delay 500; type quit; key ENTER' | Out-Null
Start-Sleep 10
& $ctl get -EndpointHost $HostName -Source 'C:\Q2Demo\baseq2\qconsole.log' -Destination (Join-Path $dir "$Tag-qconsole.log") | Out-Null
& $ctl put -EndpointHost $HostName -Source $cfg -Destination 'C:\Q2Demo\baseq2\config.cfg' | Out-Null
Get-Content (Join-Path $dir "$Tag-qconsole.log") | Select-String 'fps|frames' | ForEach-Object Line
