param([string]$Root = 'C:\everything\velocity9x\docs\probe\gen3-guard-band-2026-11-01\step2')
# Guard-band step 2, first part: the GUARD variant at a 1,024-pixel band
# (no inset), then V9XD32, Half-Life D3D and 3DMark 2000 under it.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
$s = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\4be3f200-39f9-44aa-8aa4-c90363b7ee94\scratchpad'
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
New-Item -ItemType Directory -Force -Path $Root | Out-Null
$mask = 0x100 -bor (1024 -shl 16)
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -Arguments "-omit:$mask" -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
& $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $Root 'arm-V9XSNA7.INI') | Out-Null
(Select-String -LiteralPath (Join-Path $Root 'arm-V9XSNA7.INI') -Pattern '^Omit(Requested|Accepted)=' | ForEach-Object Line) -join ' '
foreach ($ddi in '/ddi6', '/ddi5') {
    $r = J exec -Application 'C:\V9XREL\V9XD32.EXE' -Arguments $ddi -WorkingDirectory 'C:\V9XREL' -TimeoutSeconds 300
    $f = Join-Path $Root ("v9xd32-" + $ddi.TrimStart('/') + "-V9XD32.TXT")
    & $c get @conn -Source 'C:\V9XDIAG\V9XD32.TXT' -Destination $f | Out-Null
    "V9XD32 $ddi exit=$($r.ExitCode): " + ((Select-String -LiteralPath $f -Pattern '^(Result|Failures)=' | ForEach-Object Line) -join ' ')
}
& (Join-Path $s 'hlprof.ps1') -Tag 'guard' -Api d3d -OutDir (Join-Path $Root 'half-life')
& (Join-Path $s 'mark2000.ps1') -OutDir (Join-Path $Root '3dmark2000')
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
& $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $Root 'after-a-V9XSNA7.INI') | Out-Null
'part a done; mask still set'
