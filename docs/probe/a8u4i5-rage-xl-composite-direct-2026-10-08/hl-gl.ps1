param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Extra = '',
    [int]$DemoSeconds = 60,
    [string]$HostName = '10.0.1.172',
    [int]$Port = 9869
)
# Half-Life -gl windowed 640x480 on A8U4I5: a warm-up timedemo mwd5, a
# snapshot, a measured one with a screenshot halfway, a snapshot; quit.
# valve\config.cfg is saved first and put back after. Half-Life's console
# log and the ICD's log are fetched.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$hl = 'C:\SIERRA\HALF-LIFE'
$dir = Join-Path $PSScriptRoot 'hl'
# A transfer here times out now and then; three tries each.
function Get-Guest([string]$Source, [string]$Destination) {
    foreach ($try in 1..3) {
        $g = & $ctl get -EndpointHost $HostName -Port $Port -Source $Source -Destination $Destination -TimeoutSeconds 120 -Json 2>$null | ConvertFrom-Json
        if ($g -and $g.Success) { return $true }
        Start-Sleep 5
    }
    return $false
}
New-Item -ItemType Directory -Force $dir | Out-Null
$cfg = Join-Path $dir "$Tag-config.cfg"
if (-not (Get-Guest "$hl\valve\config.cfg" $cfg)) { throw 'could not save config.cfg' }
$arguments = ("-console -condebug -gl -window -w 640 -h 480 " + $Extra).Trim()
& $ctl exec -EndpointHost $HostName -Port $Port -Application "$hl\hl.exe" -Arguments $arguments -WorkingDirectory $hl -Detach | Out-Null
Start-Sleep 60
# The main menu's Console entry, in the 640x480 window at (80,60).
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'move 184,252; click left; delay 2000' | Out-Null
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type echo V9X_WARM; key ENTER; delay 500; type timedemo mwd5; key ENTER' | Out-Null
Start-Sleep ($DemoSeconds + 10)
& (Join-Path $PSScriptRoot 'snap.ps1') -Tag "$Tag-before" -GuestHost $HostName | Out-Null
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type echo V9X_RUN; key ENTER; delay 500; type timedemo mwd5; key ENTER' | Out-Null
Start-Sleep ([Math]::Floor($DemoSeconds / 2))
& $ctl screenshot -EndpointHost $HostName -Port $Port -OutFile (Join-Path $dir "$Tag-agent.bmp") -TimeoutSeconds 240 -Json | Out-Null
Start-Sleep ([Math]::Ceiling($DemoSeconds / 2) + 10)
& (Join-Path $PSScriptRoot 'snap.ps1') -Tag "$Tag-after" -GuestHost $HostName | Out-Null
& $ctl input -EndpointHost $HostName -Port $Port -Sequence 'type quit; key ENTER' | Out-Null
Start-Sleep 15
$log = Join-Path $dir "$Tag-qconsole.log"
Get-Guest "$hl\valve\qconsole.log" $log | Out-Null
Get-Guest 'C:\V9XDIAG\V9XGL.LOG' (Join-Path $dir "$Tag-V9XGL.LOG") | Out-Null
& $ctl put -EndpointHost $HostName -Port $Port -Source $cfg -Destination "$hl\valve\config.cfg" | Out-Null
Get-Content $log | Select-String 'V9X_|fps|multitex|SGIS|GL_RENDERER' | ForEach-Object Line
