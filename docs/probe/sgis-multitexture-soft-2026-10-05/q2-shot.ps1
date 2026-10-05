param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Extra = '',
    [int]$LoadSeconds = 120,
    [string]$HostName = '127.0.0.1',
    [int]$Port = 9878
)
# Quake 2 demo at demo1's spawn, one screenshot with the console closed
# (bound to K), then quit. config.cfg saved first and put back once Quake 2
# has written it on quit. Prints the new screenshot's name.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$dir = Join-Path $PSScriptRoot 'q2'
New-Item -ItemType Directory -Force $dir | Out-Null
$cfg = Join-Path $dir "$Tag-config.cfg"
$r = & $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\Q2Demo\baseq2\config.cfg' -Destination $cfg -Json | ConvertFrom-Json
if (-not $r.Success) { throw 'could not save config.cfg' }
$arguments = "+set vid_ref gl +set vid_fullscreen 0 +set gl_mode 3 +set logfile 2 +set cheats 1 $Extra +map demo1"
& $ctl exec -EndpointHost $HostName -Port $Port -Application 'C:\Q2Demo\quake2.exe' -Arguments $arguments -WorkingDirectory 'C:\Q2Demo' -Detach | Out-Null
Start-Sleep $LoadSeconds
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type `; delay 1500; type notarget; key ENTER; delay 1000; type bind k screenshot; key ENTER; delay 1000; type `; delay 6000; type k; delay 8000; type `; delay 1500; type quit; key ENTER' | Out-Null
Start-Sleep 40
& $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\Q2Demo\baseq2\qconsole.log' -Destination (Join-Path $dir "$Tag-qconsole.log") | Out-Null
& $ctl put -EndpointHost $HostName -Port $Port -Source $cfg -Destination 'C:\Q2Demo\baseq2\config.cfg' | Out-Null
$wrote = Get-Content (Join-Path $dir "$Tag-qconsole.log") | Select-String 'Wrote (quake\d+\.tga)' | Select-Object -Last 1
if ($wrote) {
    $name = $wrote.Matches[0].Groups[1].Value
    & $ctl get -EndpointHost $HostName -Port $Port -Source "C:\Q2Demo\baseq2\scrnshot\$name" -Destination (Join-Path $dir "$Tag-$name") | Out-Null
    "$Tag -> $name"
} else {
    'no screenshot'
}
Get-Content (Join-Path $dir "$Tag-qconsole.log") | Select-String 'multitexture|quit' | ForEach-Object Line
