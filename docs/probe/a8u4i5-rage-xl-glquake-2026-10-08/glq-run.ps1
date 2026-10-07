param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Extra = '',
    [int]$TexSort = 0,
    [int]$ShotSeconds = 20,
    [int]$LimitSeconds = 300,
    [string]$HostName = '10.0.1.172',
    [int]$Port = 9869
)
# GLQuake 0.97 on A8U4I5, windowed 640x480: a V9XTRACE snapshot, then
# GLQuake started straight into `timedemo demo1` with gl_texsort as given,
# a screenshot partway, the console log polled for the result, a second
# snapshot, quit. The ICD log is fetched after.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$dir = Join-Path $PSScriptRoot 'glq'
New-Item -ItemType Directory -Force $dir | Out-Null
function Get-Guest([string]$Source, [string]$Destination) {
    foreach ($try in 1..3) {
        $g = & $ctl get -EndpointHost $HostName -Port $Port -Source $Source -Destination $Destination -TimeoutSeconds 120 -Json 2>$null | ConvertFrom-Json
        if ($g -and $g.Success) { return $true }
        Start-Sleep 5
    }
    return $false
}
$log = Join-Path $dir "$Tag-qconsole.log"
# qconsole.log is appended to across runs: this run's result is the line
# after the ones already there.
$before = 0
if (Get-Guest 'C:\QUAKE\id1\qconsole.log' $log) {
    $before = @(Select-String -Path $log -Pattern 'seconds').Count
}
& (Join-Path $PSScriptRoot 'snap.ps1') -Tag "$Tag-before" -GuestHost $HostName | Out-Null
$arguments = ("-window -width 640 -height 480 -condebug $Extra +gl_texsort $TexSort +timedemo demo1").Trim()
& $ctl exec -EndpointHost $HostName -Port $Port -Application 'C:\QUAKE\GLQUAKE.EXE' -Arguments $arguments -WorkingDirectory 'C:\QUAKE' -Detach | Out-Null
Start-Sleep $ShotSeconds
& $ctl screenshot -EndpointHost $HostName -Port $Port -OutFile (Join-Path $dir "$Tag-agent.bmp") -TimeoutSeconds 240 -Json | Out-Null
$deadline = (Get-Date).AddSeconds($LimitSeconds)
do {
    Start-Sleep 10
    Get-Guest 'C:\QUAKE\id1\qconsole.log' $log | Out-Null
    $now = 0
    if (Test-Path $log) { $now = @(Select-String -Path $log -Pattern 'seconds').Count }
} while ($now -le $before -and (Get-Date) -lt $deadline)
& (Join-Path $PSScriptRoot 'snap.ps1') -Tag "$Tag-after" -GuestHost $HostName | Out-Null
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type `; delay 1500; type quit; key ENTER' | Out-Null
Start-Sleep 15
Get-Guest 'C:\QUAKE\id1\qconsole.log' $log | Out-Null
Get-Guest 'C:\V9XDIAG\V9XGL.LOG' (Join-Path $dir "$Tag-V9XGL.LOG") | Out-Null
if (Test-Path $log) {
    Get-Content $log | Select-String 'seconds|fps|multitex|Multitexture|GL_RENDERER|GL_EXTENSIONS' | ForEach-Object Line
}
