param([string]$Tag, [string]$Demo = 'auto-demo0001', [int]$DemoSeconds = 520, [string]$HostName = '10.0.1.254', [string]$Job = 'HLPROF')
# Serious Sam FE demo run on the netbook. The game's own dem_bProfile report
# does not appear in 1.05 and dem_strPostExec does not exist, so the frame
# count comes from the ICD's ten-second lines (sstimeline.py): the demo
# plays in real time, so frames over its fixed length are the measure.
# Launch, wait for the menu, snapshot, start the demo from the console,
# wait it out, snapshot, quit from the console, fetch both logs.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$dir = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\d3b4cbce-9a5f-4536-a3b0-18f7005d7768\scratchpad\ssam'
$root = 'C:\Program Files\Croteam\Serious Sam'
$trace = "C:\V9XREMOTE\JOBS\$Job\V9XTRACE.EXE"
function Snap([string]$name) {
    & $ctl exec -EndpointHost $HostName -Application $trace -WorkingDirectory "C:\V9XREMOTE\JOBS\$Job" -Detach | Out-Null
    Start-Sleep 6
    & $ctl get -EndpointHost $HostName -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $dir $name) | Out-Null
}
& $ctl exec -EndpointHost $HostName -Application "$root\Bin\SeriousSam.exe" -WorkingDirectory "$root\Bin" -Detach | Out-Null
Start-Sleep 75
Snap "$Tag-pre.ini"
& $ctl input -EndpointHost $HostName -Sequence ('type `; delay 1500; type /PlayDemo("{0}"); key ENTER; delay 1000; type `' -f $Demo) | Out-Null
Start-Sleep $DemoSeconds
Snap "$Tag-post.ini"
& $ctl input -EndpointHost $HostName -Sequence 'type `; delay 1500; type /Quit(); key ENTER' | Out-Null
Start-Sleep 25
& $ctl get -EndpointHost $HostName -Source "$root\SERIOUSSAM.log" -Destination (Join-Path $dir "$Tag-SERIOUSSAM.log") | Out-Null
& $ctl get -EndpointHost $HostName -Source 'C:\V9XDIAG\V9XGL.LOG' -Destination (Join-Path $dir "$Tag-V9XGL.log") | Out-Null
Get-Content (Join-Path $dir "$Tag-SERIOUSSAM.log") | Select-String 'demo|->' | ForEach-Object Line
