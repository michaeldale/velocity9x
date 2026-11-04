param([string]$Root = 'C:\everything\velocity9x\docs\probe\gen3-guard-band-step3-2026-11-04')
# Guard-band step 3 on the netbook: the shipped band (mask 0), after the
# WININIT deploy of e44c8882-dirty. Deploy check, V9XD32 at DDI 6 and 5,
# V9XDDP, with V9XTRACE snapshots (the new I9xxGuard* counters) between.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
$pkg = 'C:\everything\velocity9x\build\win98se-intel-gma'
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
function Snap([string]$name) {
    $null = J exec -Application 'C:\V9XREL\V9XTRC6.EXE' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
    & $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $Root "$name-V9XSNA7.INI") | Out-Null
    $f = Join-Path $Root "$name-V9XSNA7.INI"
    "$name : " + ((Select-String -LiteralPath $f -Pattern '^(BootId|BuildsAgree|DriverBuild|HalBuild|OmitAccepted|I9xxGuardCut|I9xxGuardDropped|I9xxDrawsSubmitted|I9xxDrawsRefused|I9xxRefuseLast|I9xxVertexRefusedXy|I9xxUvZeroed|BatchesEngineRefused)=' | ForEach-Object Line) -join ' ')
}
New-Item -ItemType Directory -Force -Path (Join-Path $Root 'deploy') | Out-Null

# The staged files are gone once WININIT renamed them; the installed HAL is
# the package's, by hash.
foreach ($stage in 'V9XNDRV', 'V9XNVXD', 'V9XNHAL', 'V9XNSTP', 'V9XNGL') {
    $s = J stat -RemotePath "C:\$stage.BIN"
    "C:\$stage.BIN exists=$($s.Exists)"
}
foreach ($name in 'V9XHAL.DLL', 'V9XDISP.DRV', 'V9XGL.DLL') {
    $got = Join-Path $Root "deploy\installed-$name"
    & $c get @conn -Source "C:\WINDOWS\SYSTEM\$name" -Destination $got | Out-Null
    $match = (Get-FileHash $got).Hash -eq (Get-FileHash (Join-Path $pkg $name)).Hash
    "$name matches package: $match"
    Remove-Item -LiteralPath $got
}
& $c get @conn -Source 'C:\V9XDIAG\V9XBOOT.INI' -Destination (Join-Path $Root 'deploy\V9XBOOT.INI') | Out-Null

# The shipped band: no measurement mask.
$null = J exec -Application 'C:\V9XREL\V9XTRC6.EXE' -Arguments '-omit:0' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
Snap 'pre'

& $c put @conn -Source 'C:\everything\velocity9x\build\d3d32-probe\V9XD32.EXE' -Destination 'C:\V9XREL\V9XD32.EXE' | Out-Null
foreach ($ddi in '/ddi6', '/ddi5') {
    $r = J exec -Application 'C:\V9XREL\V9XD32.EXE' -Arguments $ddi -WorkingDirectory 'C:\V9XREL' -TimeoutSeconds 300
    $f = Join-Path $Root ('v9xd32-' + $ddi.TrimStart('/') + '-V9XD32.TXT')
    & $c get @conn -Source 'C:\V9XDIAG\V9XD32.TXT' -Destination $f | Out-Null
    "V9XD32 $ddi exit=$($r.ExitCode): " + ((Select-String -LiteralPath $f -Pattern '^(Result|Failures)=' | ForEach-Object Line) -join ' ')
}
Snap 'after-v9xd32'

& $c put @conn -Source (Join-Path $pkg 'V9XDDP.EXE') -Destination 'C:\V9XREL\V9XDDP6.EXE' | Out-Null
$r = J exec -Application 'C:\V9XREL\V9XDDP6.EXE' -WorkingDirectory 'C:\V9XDIAG' -TimeoutSeconds 300
$f = Join-Path $Root 'v9xddp-V9XDD.INI'
& $c get @conn -Source 'C:\V9XDIAG\V9XDD.INI' -Destination $f | Out-Null
"V9XDDP exit=$($r.ExitCode): " + ((Select-String -LiteralPath $f -Pattern '^(Result|D3DChecksPassed|D3DChecksRun|D3DChecksFailed)=' | ForEach-Object Line) -join ' ')
Snap 'after-v9xddp'
