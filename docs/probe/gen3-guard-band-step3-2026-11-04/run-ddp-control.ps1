param([string]$Root = 'C:\everything\velocity9x\docs\probe\gen3-guard-band-step3-2026-11-04')
# Control for V9XDDP: the GUARD variant with band 0 (mask 0x100), which has
# the classifier cut every triangle off the target, as the core did before
# step 3. Same boot, same files; the mask is put back to 0 after.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
$null = J exec -Application 'C:\V9XREL\V9XTRC6.EXE' -Arguments '-omit:256' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
& $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $Root 'control-arm-V9XSNA7.INI') | Out-Null
(Select-String -LiteralPath (Join-Path $Root 'control-arm-V9XSNA7.INI') -Pattern '^Omit(Requested|Accepted)=' | ForEach-Object Line) -join ' '
$r = J exec -Application 'C:\V9XREL\V9XDDP6.EXE' -WorkingDirectory 'C:\V9XDIAG' -TimeoutSeconds 300
$f = Join-Path $Root 'control-band0-V9XDD.INI'
& $c get @conn -Source 'C:\V9XDIAG\V9XDD.INI' -Destination $f | Out-Null
"V9XDDP band 0 exit=$($r.ExitCode): " + ((Select-String -LiteralPath $f -Pattern '^Result=' | ForEach-Object Line) -join ' ')
$null = J exec -Application 'C:\V9XREL\V9XTRC6.EXE' -Arguments '-omit:0' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
& $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $Root 'control-after-V9XSNA7.INI') | Out-Null
(Select-String -LiteralPath (Join-Path $Root 'control-after-V9XSNA7.INI') -Pattern '^(Omit(Requested|Accepted)|I9xxGuardCut|I9xxGuardDropped|I9xxDrawsRefused)=' | ForEach-Object Line) -join ' '
