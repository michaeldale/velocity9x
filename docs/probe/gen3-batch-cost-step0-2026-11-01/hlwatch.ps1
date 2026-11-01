param([Parameter(Mandatory = $true)][string]$Tag,
      [string]$OutDir = 'C:\everything\velocity9x\docs\probe\gen3-batch-cost-step0-2026-11-01\half-life')
# Michael runs the timedemo; this takes a V9XTRACE snapshot as the demo
# starts playing and another as its fps line lands.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
$log = 'C:\Sierra\Half-Life\valve\qconsole.log'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
function Snap([string]$name) {
    & $c exec @conn -Application 'C:\V9XREL\V9XTRACE.EXE' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60 | Out-Null
    & $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $OutDir "$Tag-$name-V9XSNA7.INI") | Out-Null
    "$(Get-Date -Format HH:mm:ss) snap $name"
}
# cat -Until also matches a line already in the log, so the previous
# session's log is replaced first, once Half-Life has closed it.
& $c wait-process @conn -Name 'HL.EXE' -WaitSeconds 3600 | Out-Null
$placeholder = Join-Path $env:TEMP 'v9x-qc-placeholder.log'
Set-Content -LiteralPath $placeholder -Value 'V9X placeholder' -Encoding ascii
& $c put @conn -Source $placeholder -Destination $log | Out-Null
"$(Get-Date -Format HH:mm:ss) log replaced; waiting for the demo"
& $c cat @conn -Path $log -Follow -Until 'Playing demo' -WaitSeconds 3600 | Select-Object -Last 1
Snap 'pre'
& $c cat @conn -Path $log -Follow -Until 'fps' -WaitSeconds 900 | Select-Object -Last 1
Snap 'post'
& $c get @conn -Source $log -Destination (Join-Path $OutDir "$Tag-qconsole.log") | Out-Null
Select-String -LiteralPath (Join-Path $OutDir "$Tag-qconsole.log") -Pattern 'fps' | ForEach-Object Line
