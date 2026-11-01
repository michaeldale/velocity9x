param([int[]]$Masks = @(0, 64, 128, 192),
      [string]$Root = 'C:\everything\velocity9x\docs\probe\gen3-batch-cost-step1-2026-11-01',
      [switch]$SkipHalfLife,
      [switch]$SkipBench)
# Gen3 batch-cost step 1: for each omit mask, set it, check V9XD32 /ddi6,
# run DB3D at 16 bpp, then Half-Life mwd5 D3D (warm demo, then a profiled one).
$ErrorActionPreference = 'Continue'
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$conn = @{ EndpointHost = '10.0.1.6'; Port = 9869; ConnectTimeoutSeconds = 30 }
$s = 'C:\Users\michael\AppData\Local\Temp\claude\C--everything-velocity9x\4be3f200-39f9-44aa-8aa4-c90363b7ee94\scratchpad'
function J { (& $c @args @conn -Json) | ConvertFrom-Json }
foreach ($m in $Masks) {
    $out = Join-Path $Root "omit-$m"
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -Arguments "-omit:$m" -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
    & $c get @conn -Source 'C:\V9XDIAG\V9XSNA7.INI' -Destination (Join-Path $out 'arm-V9XSNA7.INI') | Out-Null
    $arm = (Select-String -LiteralPath (Join-Path $out 'arm-V9XSNA7.INI') -Pattern '^Omit(Requested|Accepted)=' | ForEach-Object Line) -join ' '
    "== omit $m : $arm"

    if (-not $SkipBench) {
    $r = J exec -Application 'C:\V9XREL\V9XD32.EXE' -Arguments '/ddi6' -WorkingDirectory 'C:\V9XREL' -TimeoutSeconds 300
    & $c get @conn -Source 'C:\V9XDIAG\V9XD32.TXT' -Destination (Join-Path $out 'v9xd32-ddi6-V9XD32.TXT') | Out-Null
    $res = (Select-String -LiteralPath (Join-Path $out 'v9xd32-ddi6-V9XD32.TXT') -Pattern '^(Result|Failures)=' | ForEach-Object Line) -join ' '
    "V9XD32 /ddi6 exit=$($r.ExitCode): $res"

    Push-Location 'C:\everything\dispbench'
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'scripts\run-bench.ps1' -ConfigId 'w9qi2be2-gma950-v9x' -Environment metal -EndpointHost '10.0.1.6' -Port 9869 -Tools DB3D -Db3dDepths 16 -OutputRoot (Join-Path $out 'dispbench') | Select-String -Pattern 'DB3D|results' | ForEach-Object Line
    Pop-Location
    }

    if (-not $SkipHalfLife) {
        & (Join-Path $s 'hlprof.ps1') -Tag "omit$m" -Api d3d -OutDir (Join-Path $out 'half-life')
    }
}
$null = J exec -Application 'C:\V9XREL\V9XTRACE.EXE' -Arguments '-omit:0' -WorkingDirectory 'C:\V9XREL' -WaitForExit -WaitSeconds 60
'mask back to 0'
