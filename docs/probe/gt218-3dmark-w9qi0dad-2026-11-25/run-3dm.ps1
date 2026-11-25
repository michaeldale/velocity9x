param(
    [Parameter(Mandatory = $true)][ValidateSet('2000', '2001')][string]$Which,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [int]$Minutes = 40
)
# 3DMark 2000 or 2001 SE default benchmark on W9QI0DAD to its score, no
# frame grabs (each freezes the game about 2 s). V9XTRACE snapshots before
# and after; a -beat every loop watches FlipHandled for a freeze.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$h = '10.0.1.172'
$tools = 'C:\V9XNVP'
if ($Which -eq '2000') {
    $exe = 'C:\Program Files\MadOnion.com\3DMark2000\3DMark2000.exe'
    $prefix = '3DMark2000'; $proc = '3DMARK2000.EXE'
} else {
    $exe = 'C:\Program Files\MadOnion.com\3DMark2001 SE\3DMark2001SE.exe'
    $prefix = '3DMark2001'; $proc = '3DMARK2001SE.EXE'
}
$dir = Split-Path -Parent $exe
New-Item -ItemType Directory -Force $OutDir | Out-Null
function Log([string]$t) { $line = "{0:HH:mm:ss} {1}" -f (Get-Date), $t; $line; Add-Content -LiteralPath (Join-Path $OutDir 'run.log') $line }
function Snap([string]$name) {
    & $c exec -Host $h -Application "$tools\V9XTRACE.EXE" -WorkingDirectory $tools -WaitForExit -WaitSeconds 120 | Out-Null
    & $c get -Host $h -Source 'C:\V9XDIAG\V9XSNAL.TXT' -Destination (Join-Path $OutDir 'snal.txt') | Out-Null
    $p = @(Get-Content -LiteralPath (Join-Path $OutDir 'snal.txt') | Where-Object { $_.Trim() })[0]
    if ($p) { & $c get -Host $h -Source $p.Trim() -Destination (Join-Path $OutDir $name) | Out-Null }
}
function Windows3d { $w = & $c windows -Host $h -Json 2>$null | ConvertFrom-Json; if ($w) { @($w.Windows | Where-Object { $_.Title }) } else { $null } }

& $c note -Host $h -Text "Claude: 3DMark $Which run, no grabs" | Out-Null
Snap 'snapshot-before.ini'
& $c exec -Host $h -Application $exe -WorkingDirectory $dir -Detach | Out-Null
$m = & $c wait-window -Host $h -Title $prefix -WaitSeconds 120 -Json | ConvertFrom-Json
if (-not $m.Success) { Log 'menu never opened'; exit 2 }
Start-Sleep -Seconds 5
$started = $false
for ($try = 0; $try -lt 6 -and -not $started; ++$try) {
    & $c input -Host $h -Sequence 'key ALT+B' | Out-Null
    for ($look = 0; $look -lt 3 -and -not $started; ++$look) {
        Start-Sleep -Seconds 5
        $w = Windows3d
        $started = ($w -ne $null -and -not @($w | Where-Object { $_.Class -eq '#32770' -and $_.Title -clike "$prefix*" }))
    }
}
if (-not $started) { & $c screenshot -Host $h -OutFile (Join-Path $OutDir 'stuck.png') | Out-Null; Log 'start input not taken'; exit 3 }
Log "started after $try input(s)"
$began = Get-Date; $lastFlips = $null; $moved = Get-Date; $result = 'running'
while ($true) {
    $min = ((Get-Date) - $began).TotalMinutes
    if ($min -gt $Minutes) { $result = "no score after $Minutes min"; break }
    $s = & $c wait-window -Host $h -Title 'Overall Score' -WaitSeconds 20 -Json 2>$null | ConvertFrom-Json
    if ($s -and $s.Success) { $result = ('score after {0:N1} min' -f $min); break }
    & $c exec -Host $h -Application "$tools\V9XTRACE.EXE" -Arguments '-beat' -WorkingDirectory $tools -WaitForExit -WaitSeconds 30 2>$null | Out-Null
    & $c get -Host $h -Source 'C:\V9XDIAG\V9XBEAT.INI' -Destination (Join-Path $OutDir 'beat.ini') 2>$null | Out-Null
    $flips = @(Get-Content -LiteralPath (Join-Path $OutDir 'beat.ini') -ErrorAction SilentlyContinue | Where-Object { $_ -match '^FlipHandled=\d+' } | ForEach-Object { ($_ -replace 'FlipHandled=', '').Trim() })[0]
    if ($flips -and $flips -ne $lastFlips) { $lastFlips = $flips; $moved = Get-Date }
    Log ("+{0:N1} min FlipHandled {1}" -f $min, $lastFlips)
    if (((Get-Date) - $moved).TotalSeconds -gt 150) { $result = "frozen at FlipHandled $lastFlips"; break }
    $ps = & $c ps -Host $h -Json 2>$null | ConvertFrom-Json
    if ($ps -and -not @($ps.Processes | Where-Object { $_.Name -eq $proc })) { $result = '3DMark process gone'; break }
    $w = Windows3d
    if ($w) {
        $foreign = @($w | Where-Object { $_.Class -eq '#32770' -and $_.Title -cnotlike "$prefix*" })
        if ($foreign) { $result = "dialog '$($foreign[0].Title)'"; break }
    }
}
Log "RESULT: $result"
& $c windows -Host $h -Controls -Json 2>$null | Set-Content -LiteralPath (Join-Path $OutDir 'score-window.json')
& $c screenshot -Host $h -OutFile (Join-Path $OutDir 'end.png') | Out-Null
& $c close-window -Host $h -Title 'Overall Score' 2>$null | Out-Null
& $c kill -Host $h -Name $proc 2>$null | Out-Null
Snap 'snapshot-after.ini'
Log 'done'
