param([string]$Root = 'C:\everything\velocity9x\docs\probe\gen3-guard-band-step3-2026-11-04')
# 3DMark 2000 and UT99's CityIntro under the shipped band (mask 0), with the
# step 2 drivers of each (docs\probe\gen3-guard-band-2026-11-01), and a
# V9XTRC6 snapshot after each for the I9xxGuard* counters.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
$step2 = 'C:\everything\velocity9x\docs\probe\gen3-guard-band-2026-11-01'
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
function Snap([string]$name) {
    $null = J exec -Application 'C:\V9XREL\V9XTRC6.EXE' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
    $f = Join-Path $Root "$name-V9XSNA7.INI"
    & $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination $f | Out-Null
    "$name : " + ((Select-String -LiteralPath $f -Pattern '^(BootId|OmitAccepted|I9xxGuardCut|I9xxGuardDropped|I9xxDrawsSubmitted|I9xxDrawsRefused|I9xxRefuseLast|I9xxVertexRefusedXy|BatchesEngineRefused|R3dListClipped)=' | ForEach-Object Line) -join ' ')
}
Snap 'games-pre'
& (Join-Path $step2 'mark2000.ps1') -OutDir (Join-Path $Root '3dmark2000')
Snap 'after-3dmark2000'
& (Join-Path $step2 'utrun.ps1') -Tag 'ut99' -Root $Root
Snap 'after-ut99'
