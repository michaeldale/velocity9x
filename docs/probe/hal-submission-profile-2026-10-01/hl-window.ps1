param([string]$Tag, [string]$Renderer = '', [int]$DemoSeconds = 16, [string]$HostName = '10.0.1.254', [string]$Job = 'FLIPTIME')
# Half-Life on the netbook: launch, open the console, one warm-up timedemo,
# then a snapshot immediately before and after a second timedemo so the
# window is (nearly) the demo alone. $Renderer is '' (as configured) or '-gl'.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$dir = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\d3b4cbce-9a5f-4536-a3b0-18f7005d7768\scratchpad'
$trace = "C:\V9XREMOTE\JOBS\$Job\V9XTRACE.EXE"
function Snap([string]$name) {
    & $ctl exec -EndpointHost $HostName -Application $trace -WorkingDirectory "C:\V9XREMOTE\JOBS\$Job" -Detach | Out-Null
    Start-Sleep 4
    & $ctl get -EndpointHost $HostName -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $dir $name) | Out-Null
}
$args = ('-console -condebug ' + $Renderer).Trim()
& $ctl exec -EndpointHost $HostName -Application 'C:\SIERRA\Half-Life\hl.exe' -Arguments $args -WorkingDirectory 'C:\SIERRA\Half-Life' -Detach | Out-Null
Start-Sleep 45
& $ctl input -EndpointHost $HostName -Sequence 'move 103,192; click left' | Out-Null
Start-Sleep 3
& $ctl input -EndpointHost $HostName -Sequence "type  echo V9X_${Tag}_WARM; key ENTER; delay 500; type timedemo mwd5; key ENTER" | Out-Null
Start-Sleep ($DemoSeconds + 14)
Snap "$Tag-idle0.ini"
Start-Sleep 15
Snap "$Tag-pre.ini"
& $ctl input -EndpointHost $HostName -Sequence "type echo V9X_${Tag}_RUN; key ENTER; delay 300; type timedemo mwd5; key ENTER" | Out-Null
Start-Sleep $DemoSeconds
Snap "$Tag-post.ini"
& $ctl input -EndpointHost $HostName -Sequence 'type quit; key ENTER' | Out-Null
Start-Sleep 8
& $ctl get -EndpointHost $HostName -Source 'C:\SIERRA\Half-Life\valve\qconsole.log' -Destination (Join-Path $dir "$Tag-qconsole.log") | Out-Null
Get-Content (Join-Path $dir "$Tag-qconsole.log") | Select-String 'V9X_|fps|OpenGL|Direct3D|GL_' | ForEach-Object Line
