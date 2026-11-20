param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [ValidateSet('', '0', '1', '2')][string]$VSync = '',
    [int]$Minutes = 40,
    [switch]$Grab,
    [string]$GrabArg = '-grab:40x10',
    [int]$ClickX = 463,
    [int]$ClickY = 424
)
# 3DMark 2001 SE on M-NZHWBR (HD 3000) over the relay, with V9XTRACE
# snapshots around it. The HD 4600's hsw-3dm2001.ps1 and vsync-run.ps1
# (docs\probe\hd4600-vsync-off-2026-11-19) for -Relay. -VSync sets
# [Velocity9x] VSync= in SYSTEM.INI first (read at driver-object creation);
# the file as found is kept as SYSTEM-before.INI for restoring.
# Never call `windows -Controls` without -Title: it can dump secrets.
$ErrorActionPreference = 'Stop'
$c = Join-Path $PSScriptRoot 'agent-scripts\v9xctl.ps1'
$env:V9XSIGN = 'C:\everything\v9x-remote-agent\build\tools\v9xsign.exe'
$r = 'M-NZHWBR'
$tools = 'C:\V9XDIAG'
New-Item -ItemType Directory -Force $OutDir | Out-Null
function Log([string]$t) { $line = "{0:HH:mm:ss} {1}" -f (Get-Date), $t; $line; Add-Content -LiteralPath (Join-Path $OutDir 'run.log') $line }
function Snap([string]$name) {
    & $c exec -Relay $r -Application "$tools\V9XTRACE.EXE" -WorkingDirectory $tools -WaitForExit -WaitSeconds 120 | Out-Null
    $latest = (& $c cat -Relay $r -Path 'C:\V9XDIAG\V9XSNAL.TXT' -Json 2>$null | ConvertFrom-Json).Text -split "`r?`n"
    $p = @($latest | Where-Object { $_.Trim() })[0]
    if ($p) { & $c get -Relay $r -Source $p.Trim() -Destination (Join-Path $OutDir $name) | Out-Null }
}
function Windows3d { $w = & $c windows -Relay $r -Json 2>$null | ConvertFrom-Json; if ($w) { @($w.Windows | Where-Object { $_.Title }) } else { $null } }

if ($VSync -ne '') {
    $before = Join-Path $OutDir 'SYSTEM-before.INI'
    & $c get -Relay $r -Source 'C:\WINDOWS\SYSTEM.INI' -Destination $before | Out-Null
    $lines = [System.Collections.Generic.List[string]]::new()
    $section = ''
    $found = $false
    foreach ($line in (Get-Content -LiteralPath $before)) {
        if ($line -match '^\s*\[(.+)\]\s*$') { $section = $Matches[1] }
        if ($section -eq 'Velocity9x' -and $line -match '^\s*VSync\s*=') { continue }
        $lines.Add($line)
        if ($line -match '^\s*\[Velocity9x\]\s*$') { $lines.Add("VSync=$VSync"); $found = $true }
    }
    if (-not $found) { $lines.Add('[Velocity9x]'); $lines.Add("VSync=$VSync") }
    $after = Join-Path $OutDir 'SYSTEM-set.INI'
    [System.IO.File]::WriteAllText($after, (($lines -join "`r`n") + "`r`n"), [System.Text.Encoding]::GetEncoding(1252))
    & $c put -Relay $r -Source $after -Destination 'C:\WINDOWS\SYSTEM.INI' | Out-Null
    $check = (& $c cat -Relay $r -Path 'C:\WINDOWS\SYSTEM.INI' -Json | ConvertFrom-Json).Text
    if ($check -notmatch "(?m)^VSync=$VSync\s*$") { throw 'VSync= did not reach SYSTEM.INI' }
    Log "VSync=$VSync set"
}
# From here on a wait that times out is an answer, not a failure: the
# Haswell runner ran without Stop, and a 20-second wait-window for the score
# window ends in an error record every time it is not up yet.
$ErrorActionPreference = 'Continue'

Snap 'snapshot-before.ini'
if ($Grab) { & $c exec -Relay $r -Application "$tools\V9XTRACE.EXE" -Arguments $GrabArg -WorkingDirectory $tools -WaitForExit -WaitSeconds 120 | Out-Null }
& $c exec -Relay $r -Application 'C:\Program Files\MadOnion.com\3DMark2001 SE\3DMark2001SE.exe' -WorkingDirectory 'C:\Program Files\MadOnion.com\3DMark2001 SE' -Detach | Out-Null
$m = & $c wait-window -Relay $r -Title '3DMark2001' -WaitSeconds 180 -Json | ConvertFrom-Json
if (-not $m.Success) { Log 'menu never opened'; exit 2 }
Start-Sleep -Seconds 5
& $c screenshot -Relay $r -OutFile (Join-Path $OutDir 'menu.png') | Out-Null
$started = $false
for ($try = 0; $try -lt 6 -and -not $started; ++$try) {
    & $c close-window -Relay $r -Title 'Mprexe' 2>$null | Out-Null
    & $c input -Relay $r -Sequence "move $ClickX,$ClickY; click left" | Out-Null
    for ($look = 0; $look -lt 3 -and -not $started; ++$look) {
        & $c wait-window -Relay $r -Title '3DMark - Display Window' -WaitSeconds 5 2>$null | Out-Null
        $w = Windows3d
        $started = ($w -ne $null -and -not @($w | Where-Object { $_.Class -eq '#32770' -and $_.Title -clike '3DMark2001*' }))
    }
}
if (-not $started) { & $c screenshot -Relay $r -OutFile (Join-Path $OutDir 'stuck.png') -TimeoutSeconds 240 | Out-Null; Log 'start input not taken'; exit 3 }
Log "started after $try input(s)"
$began = Get-Date; $lastFlips = $null; $moved = Get-Date; $result = 'running'
while ($true) {
    $min = ((Get-Date) - $began).TotalMinutes
    if ($min -gt $Minutes) { $result = "no score after $Minutes min"; break }
    $s = & $c wait-window -Relay $r -Title 'Overall Score' -WaitSeconds 20 -Json 2>$null | ConvertFrom-Json
    if ($s -and $s.Success) { $result = ('score after {0:N1} min' -f $min); break }
    & $c exec -Relay $r -Application "$tools\V9XTRACE.EXE" -Arguments '-beat' -WorkingDirectory $tools -WaitForExit -WaitSeconds 30 2>$null | Out-Null
    $beat = (& $c cat -Relay $r -Path 'C:\V9XDIAG\V9XBEAT.INI' -Json 2>$null | ConvertFrom-Json).Text -split "`r?`n"
    $flips = @($beat | Where-Object { $_ -match '^FlipHandled=\d+' } | ForEach-Object { ($_ -replace 'FlipHandled=', '').Trim() })[0]
    if ($flips -and $flips -ne $lastFlips) { $lastFlips = $flips; $moved = Get-Date }
    Log ("+{0:N1} min FlipHandled {1}" -f $min, $lastFlips)
    if (((Get-Date) - $moved).TotalSeconds -gt 180) { $result = "frozen at FlipHandled $lastFlips"; break }
    $ps = & $c ps -Relay $r -Json 2>$null | ConvertFrom-Json
    if ($ps -and -not @($ps.Processes | Where-Object { $_.Name -eq '3DMARK2001SE.EXE' })) { $result = '3DMark process gone'; break }
    $w = Windows3d
    if ($w) {
        $foreign = @($w | Where-Object { $_.Class -eq '#32770' -and $_.Title -cnotlike '3DMark2001*' })
        if ($foreign) { $result = "dialog '$($foreign[0].Title)'"; break }
    }
}
Log "RESULT: $result"
& $c screenshot -Relay $r -OutFile (Join-Path $OutDir 'score.png') | Out-Null
& $c kill -Relay $r -Name '3DMARK2001SE.EXE' 2>$null | Out-Null
Snap 'snapshot-after.ini'
if ($Grab) { & $c pull-tree -Relay $r -Source 'C:\V9XDIAG' -Destination $OutDir -Filter 'V9XGRAB?.BMP' 2>$null | Out-Null }
Log 'done'
