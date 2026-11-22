param(
    [string]$TargetHost,
    [int]$Port = 0,
    [string]$Relay,
    [string]$OutDir,
    [int]$Rounds = 3
)
# B8 of docs/plans/release-1.0.0.md: three HAL clients started together
# (V9XGLP OpenGL in a window, V9XWPRS DirectDraw and OpenGL presents in a
# window, V9XR3DP through the render interface), each round's overlap read
# from the process list, then every report and the driver's counters.
$ErrorActionPreference = 'Stop'
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$c = if ($Relay) { @{ Relay = $Relay } } else { @{ Host = $TargetHost } }
if ($Port) { $c.Port = $Port }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$placeholder = Join-Path $OutDir 'placeholder.ini'
Set-Content -LiteralPath $placeholder -Value "; placeholder from concurrent.ps1`r`n" -Encoding Ascii
$probes = @(
    @{ Exe = 'V9XGLP.EXE'; Report = 'V9XGLP.INI' }
    @{ Exe = 'V9XWPRS.EXE'; Report = 'V9XWPRS.INI' }
    @{ Exe = 'V9XR3DP.EXE'; Report = 'V9XR3DP.INI' }
)
for ($round = 1; $round -le $Rounds; ++$round) {
    foreach ($p in $probes) {
        & $ctl put @c -Source $placeholder -Destination ("C:\V9XDIAG\" + $p.Report) -ConnectTimeoutSeconds 120 -Json | Out-Null
    }
    $started = Get-Date
    foreach ($p in $probes) {
        & $ctl exec @c -Application ("C:\V9XREL\" + $p.Exe) -Detach -ConnectTimeoutSeconds 120 -Json | Out-Null
        "round $round started $($p.Exe) at +{0:N1} s" -f ((Get-Date) - $started).TotalSeconds
    }
    $running = @((& $ctl ps @c -Json | ConvertFrom-Json).Processes | Where-Object { $_.Name -match '^V9X(GLP|WPRS|R3DP)\.EXE$' } | ForEach-Object { $_.Name })
    "round $round running together at +{0:N1} s: {1}" -f ((Get-Date) - $started).TotalSeconds, ($running -join ', ')
    foreach ($p in $probes) {
        $w = & $ctl wait-fault @c -Name $p.Exe -WaitSeconds 300 -Json 2>$null | Out-String
        "round $round $($p.Exe) wait-fault exit $LASTEXITCODE"
        if ($LASTEXITCODE -eq 50) {
            "  " + ($w -replace '\s+', ' ')
        }
    }
    foreach ($p in $probes) {
        $local = Join-Path $OutDir ("r{0}-{1}" -f $round, $p.Report)
        & $ctl get @c -Source ("C:\V9XDIAG\" + $p.Report) -Destination $local -ConnectTimeoutSeconds 120 -Json | Out-Null
        $lines = @(Select-String -LiteralPath $local -Pattern '^(Result|[A-Za-z]+Result)=' | ForEach-Object { $_.Line })
        "round $round $($p.Report): " + ($lines -join ' ')
    }
}
& $ctl exec @c -Application 'C:\WINDOWS\SYSTEM\V9XTRACE.EXE' -WaitForExit -WaitSeconds 120 -Json | Out-Null
$name = (& $ctl cat @c -Path 'C:\V9XDIAG\V9XSNAL.TXT' 2>$null | Out-String).Trim()
if ($name) {
    $snap = Join-Path $OutDir 'after-V9XSNA.INI'
    & $ctl get @c -Source $name -Destination $snap -ConnectTimeoutSeconds 120 -Json | Out-Null
    Select-String -LiteralPath $snap -Pattern '(Timeouts|Resets|Quarantin|Fault)[A-Za-z]*=' | ForEach-Object { 'counter ' + $_.Line } | Select-Object -Unique
}
'done'
