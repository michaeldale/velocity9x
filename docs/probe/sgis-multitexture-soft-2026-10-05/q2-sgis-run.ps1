param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [Parameter(Mandatory = $true)][int]$Multitexture,
    [int]$LoadSeconds = 120,
    [int]$RefreshSeconds = 150,
    [string]$HostName = '127.0.0.1',
    [int]$Port = 9878
)
# Quake 2 demo at demo1's spawn: a screenshot of the spawn view and a
# timerefresh, with gl_ext_multitexture as given. config.cfg is saved
# first and put back after, because Quake 2 writes its +set cvars there on
# quit. The ICD's log is fetched too (C:\V9XDIAG\V9XGL.LOG).
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$dir = Join-Path $PSScriptRoot 'q2'
New-Item -ItemType Directory -Force $dir | Out-Null
$cfg = Join-Path $dir "$Tag-config.cfg"
$r = & $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\Q2Demo\baseq2\config.cfg' -Destination $cfg -Json | ConvertFrom-Json
if (-not $r.Success) { throw 'could not save config.cfg' }
$arguments = "+set vid_ref gl +set vid_fullscreen 0 +set gl_mode 3 +set logfile 2 +set cheats 1 +set gl_ext_multitexture $Multitexture +map demo1"
& $ctl exec -EndpointHost $HostName -Port $Port -Application 'C:\Q2Demo\quake2.exe' -Arguments $arguments -WorkingDirectory 'C:\Q2Demo' -Detach | Out-Null
Start-Sleep $LoadSeconds
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type `; delay 1500; type notarget; key ENTER; delay 1500; type screenshot; key ENTER; delay 4000; type timerefresh; key ENTER' | Out-Null
# timerefresh holds the console until its 128 frames are drawn: wait for its
# line in the log, then quit, then wait for Quake 2 to write config.cfg.
$log = Join-Path $dir "$Tag-qconsole.log"
$deadline = (Get-Date).AddSeconds($RefreshSeconds)
do {
    Start-Sleep 20
    & $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\Q2Demo\baseq2\qconsole.log' -Destination $log | Out-Null
} while (-not (Select-String -Path $log -Pattern 'seconds' -Quiet) -and (Get-Date) -lt $deadline)
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type quit; key ENTER' | Out-Null
Start-Sleep 25
& $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\Q2Demo\baseq2\qconsole.log' -Destination (Join-Path $dir "$Tag-qconsole.log") | Out-Null
& $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\V9XDIAG\V9XGL.LOG' -Destination (Join-Path $dir "$Tag-V9XGL.LOG") | Out-Null
& $ctl put -EndpointHost $HostName -Port $Port -Source $cfg -Destination 'C:\Q2Demo\baseq2\config.cfg' | Out-Null
Get-Content (Join-Path $dir "$Tag-qconsole.log") | Select-String 'multitexture|SGIS|seconds|fps|Wrote|GL_' | ForEach-Object Line
