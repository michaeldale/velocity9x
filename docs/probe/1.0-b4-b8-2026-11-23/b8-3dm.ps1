param(
    [string]$TargetHost,
    [string]$Relay,
    [string]$OutDir,
    [int]$WatchMinutes = 30
)
# B8 of docs/plans/release-1.0.0.md: release probes (V9XGLP, V9XWPRS,
# V9XR3DP, one at a time, as their lock requires) while 3DMark 2001 SE, a
# Direct3D 8 program in exclusive full screen, is drawing its benchmark.
# 3DMark must already be open at its main window.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$c = if ($Relay) { @{ Relay = $Relay } } else { @{ Host = $TargetHost } }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$placeholder = Join-Path $OutDir 'placeholder.ini'
Set-Content -LiteralPath $placeholder -Value "; placeholder from b8-3dm.ps1`r`n" -Encoding Ascii
function Stamp { Get-Date -Format HH:mm:ss }

& $ctl input @c -Sequence 'key ALT+B' -Window '3DMark2001 SE' -ConnectTimeoutSeconds 120 -Json | Out-Null
"$(Stamp) benchmark asked for"
$w = & $ctl wait-window @c -Title 'Display Window' -WaitSeconds 180 -ConnectTimeoutSeconds 120 -Json 2>$null | Out-String
"$(Stamp) display window: exit $LASTEXITCODE"
# Into the first game test before the probes start.
& $ctl wait-fault @c -Name '3DMark2001SE.exe' -WaitSeconds 45 -ConnectTimeoutSeconds 120 -Json 2>$null | Out-Null

foreach ($p in @(@{ Exe = 'V9XGLP.EXE'; Report = 'V9XGLP.INI' },
                 @{ Exe = 'V9XWPRS.EXE'; Report = 'V9XWPRS.INI' },
                 @{ Exe = 'V9XR3DP.EXE'; Report = 'V9XR3DP.INI' })) {
    & $ctl put @c -Source $placeholder -Destination ("C:\V9XDIAG\" + $p.Report) -ConnectTimeoutSeconds 120 -Json | Out-Null
    $alive = @((& $ctl ps @c -Name '3DMark2001SE.exe' -ConnectTimeoutSeconds 120 -Json | ConvertFrom-Json).Processes).Count
    $r = & $ctl exec @c -Application ("C:\V9XREL\" + $p.Exe) -WaitForExit -WaitSeconds 300 -ConnectTimeoutSeconds 120 -Json 2>$null | ConvertFrom-Json
    $local = Join-Path $OutDir $p.Report
    & $ctl get @c -Source ("C:\V9XDIAG\" + $p.Report) -Destination $local -ConnectTimeoutSeconds 120 -Json | Out-Null
    $lines = @(Select-String -LiteralPath $local -Pattern '^(Result|[A-Za-z]+Result)=' -ErrorAction SilentlyContinue | ForEach-Object { $_.Line })
    "$(Stamp) $($p.Exe) with 3DMark running ($alive): exit $($r.ExitCode); " + ($lines -join ' ')
}

$deadline = (Get-Date).AddMinutes($WatchMinutes)
while ((Get-Date) -lt $deadline) {
    & $ctl wait-fault @c -Name '3DMark2001SE.exe' -WaitSeconds 60 -ConnectTimeoutSeconds 120 -Json 2>$null | Out-Null
    $code = $LASTEXITCODE
    if ($code -eq 0 -or $code -eq 50) {
        "$(Stamp) 3DMark ended or faulted: wait-fault exit $code"
        break
    }
    $wins = @((& $ctl windows @c -ConnectTimeoutSeconds 120 -Json | ConvertFrom-Json).Windows)
    $score = $wins | Where-Object { $_.Title -match 'Score' -or ($_.Class -eq '#32770' -and $_.Title -notmatch 'NETGEAR') }
    if ($score) {
        "$(Stamp) dialog: " + (($score | ForEach-Object { $_.Title }) -join ' | ')
        $ctrl = & $ctl windows @c -Title $score[0].Title -Controls -ConnectTimeoutSeconds 120 -Json | Out-String
        Set-Content -LiteralPath (Join-Path $OutDir 'score-dialog.json') -Value $ctrl
        break
    }
}
& $ctl exec @c -Application 'C:\V9XREL\V9XTRACE.EXE' -WaitForExit -WaitSeconds 120 -ConnectTimeoutSeconds 120 -Json | Out-Null
$name = ((& $ctl cat @c -Path 'C:\V9XDIAG\V9XSNAL.TXT' -ConnectTimeoutSeconds 120 -Json | ConvertFrom-Json).Text).Trim()
if ($name) {
    $snap = Join-Path $OutDir 'after-V9XSNA.INI'
    & $ctl get @c -Source $name -Destination $snap -ConnectTimeoutSeconds 120 -Json | Out-Null
    Select-String -LiteralPath $snap -Pattern '(Timeouts|Resets|Quarantin)[A-Za-z]*=' | ForEach-Object { 'counter ' + $_.Line } | Select-Object -Unique
}
"$(Stamp) done"
