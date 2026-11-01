param([string]$Root = 'C:\everything\velocity9x\docs\probe\gma950-3dmark2001se-refused-2026-11-01')
# Deploy the refusal-split build, then 3DMark 2001 SE's default benchmark
# with the mask at 0, snapshotted either side.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
$s = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\4be3f200-39f9-44aa-8aa4-c90363b7ee94\scratchpad'
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
New-Item -ItemType Directory -Force -Path $Root | Out-Null
& (Join-Path $s 'deploycheck.ps1') -Target '10.0.1.6:9869' -Family 'intel-gma' -OutDir (Join-Path $Root 'deploy') -D32Runs '/ddi6','/ddi5'
$null = J wait-window -Title 'NETGEAR' -WaitSeconds 120
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -Arguments '-omit:0' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
& $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $Root 'pre-V9XSNA7.INI') | Out-Null
(Select-String -LiteralPath (Join-Path $Root 'pre-V9XSNA7.INI') -Pattern '^(Omit(Requested|Accepted)|HalBuild|I9xxVertexRefusedZ)=' | ForEach-Object Line) -join ' '
$null = J exec -Application 'C:\Program Files\MadOnion.com\3DMark2001 SE\3DMark2001SE.exe' -WorkingDirectory 'C:\Program Files\MadOnion.com\3DMark2001 SE' -Detach
$null = J wait-window -Title 'Untitled - 3DMark2001' -WaitSeconds 180
Start-Sleep -Seconds 5
& (Join-Path $s 'mark2001.ps1') -OutDir $Root
