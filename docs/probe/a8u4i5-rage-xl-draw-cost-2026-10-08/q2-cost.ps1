param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [int]$LoadSeconds = 120,
    [int]$RefreshSeconds = 240,
    [string]$HostName = '10.0.1.172',
    [int]$Port = 9869,
    [int]$Mode = 3,
    [int]$Multitexture = 1
)
# Quake 2 demo at demo1's spawn on A8U4I5, timerefresh bracketed by two
# V9XTRACE snapshots, so the HAL's cost counters subtract to the refresh
# (plus the few frames either side). config.cfg saved and put back.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$dir = Join-Path $PSScriptRoot 'q2'
New-Item -ItemType Directory -Force $dir | Out-Null
$cfg = Join-Path $dir "$Tag-config.cfg"
$r = & $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\Q2DEMO\baseq2\config.cfg' -Destination $cfg -Json | ConvertFrom-Json
if (-not $r.Success) { throw 'could not save config.cfg' }
$arguments = "+set vid_ref gl +set vid_fullscreen 0 +set gl_mode $Mode +set logfile 2 +set cheats 1 +set gl_ext_multitexture $Multitexture +map demo1"
& $ctl exec -EndpointHost $HostName -Port $Port -Application 'C:\Q2DEMO\QUAKE2.EXE' -Arguments $arguments -WorkingDirectory 'C:\Q2DEMO' -Detach | Out-Null
Start-Sleep $LoadSeconds
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type `; delay 1500; type notarget; key ENTER; delay 1500' | Out-Null
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type `; delay 3000' | Out-Null
& $ctl screenshot -EndpointHost $HostName -Port $Port -OutFile (Join-Path $dir "$Tag-agent.bmp") -Json | Out-Null
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type `; delay 1500' | Out-Null
& (Join-Path $PSScriptRoot 'snap.ps1') -Tag "$Tag-before" -GuestHost $HostName | Out-Null
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type timerefresh; key ENTER' | Out-Null
$log = Join-Path $dir "$Tag-qconsole.log"
$deadline = (Get-Date).AddSeconds($RefreshSeconds)
do {
    Start-Sleep 5
    & $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\Q2DEMO\baseq2\qconsole.log' -Destination $log | Out-Null
} while (-not (Select-String -Path $log -Pattern 'seconds' -Quiet) -and (Get-Date) -lt $deadline)
& (Join-Path $PSScriptRoot 'snap.ps1') -Tag "$Tag-after" -GuestHost $HostName | Out-Null
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type quit; key ENTER' | Out-Null
Start-Sleep 25
& $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\Q2DEMO\baseq2\qconsole.log' -Destination $log | Out-Null
& $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\V9XDIAG\V9XGL.LOG' -Destination (Join-Path $dir "$Tag-V9XGL.LOG") | Out-Null
& $ctl put -EndpointHost $HostName -Port $Port -Source $cfg -Destination 'C:\Q2DEMO\baseq2\config.cfg' | Out-Null
Get-Content $log | Select-String 'seconds|fps' | ForEach-Object Line
