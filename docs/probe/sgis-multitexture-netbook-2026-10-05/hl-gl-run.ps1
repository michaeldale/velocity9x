param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Extra = '',
    [int]$DemoSeconds = 30,
    [string]$HostName = '10.0.1.254',
    [int]$Port = 9869
)
# Half-Life -gl on the netbook: launch, a warm-up timedemo mwd5, then two
# measured ones; quit. valve\config.cfg is saved first and put back after.
# The ICD's log and Half-Life's console log are fetched.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$dir = Join-Path $PSScriptRoot 'hl'
New-Item -ItemType Directory -Force $dir | Out-Null
$cfg = Join-Path $dir "$Tag-config.cfg"
$r = & $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\SIERRA\Half-Life\valve\config.cfg' -Destination $cfg -Json | ConvertFrom-Json
if (-not $r.Success) { throw 'could not save config.cfg' }
$arguments = ('-console -condebug -gl ' + $Extra).Trim()
& $ctl exec -EndpointHost $HostName -Port $Port -Application 'C:\SIERRA\Half-Life\hl.exe' -Arguments $arguments -WorkingDirectory 'C:\SIERRA\Half-Life' -Detach | Out-Null
Start-Sleep 45
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'move 103,192; click left' | Out-Null
Start-Sleep 3
foreach ($pass in 'WARM', 'RUN1', 'RUN2') {
    & $ctl input -EndpointHost $HostName -Port $Port -Sequence "type echo V9X_${Tag}_$pass; key ENTER; delay 500; type timedemo mwd5; key ENTER" | Out-Null
    Start-Sleep ($DemoSeconds + 10)
}
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type quit; key ENTER' | Out-Null
Start-Sleep 15
& $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\SIERRA\Half-Life\valve\qconsole.log' -Destination (Join-Path $dir "$Tag-qconsole.log") | Out-Null
& $ctl get -EndpointHost $HostName -Port $Port -Source 'C:\V9XDIAG\V9XGL.LOG' -Destination (Join-Path $dir "$Tag-V9XGL.LOG") | Out-Null
& $ctl put -EndpointHost $HostName -Port $Port -Source $cfg -Destination 'C:\SIERRA\Half-Life\valve\config.cfg' | Out-Null
Get-Content (Join-Path $dir "$Tag-qconsole.log") | Select-String 'V9X_|fps|multitex|SGIS|GL_EXT|GL_VENDOR|GL_RENDERER' | ForEach-Object Line
