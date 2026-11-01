param([Parameter(Mandatory = $true)][string]$OutDir)
# 3DMark 2001 SE once more from its score window: close it, snapshot, then
# the benchmark as mark2001.ps1 runs it.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
$s = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\4be3f200-39f9-44aa-8aa4-c90363b7ee94\scratchpad'
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
& $c screenshot @conn -OutFile (Join-Path $OutDir 'score-before.png') | Out-Null
$null = J input -Sequence 'move 612,400; click left; delay 2000'
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
& $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $OutDir 'pre-V9XSNA7.INI') | Out-Null
& (Join-Path $s 'mark2001.ps1') -OutDir $OutDir
