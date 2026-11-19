param([Parameter(Mandatory = $true)][string]$OutDir, [int]$Minutes = 40, [switch]$Grab, [string]$GrabArg = '-grab:40x10')
# 3DMark 2001 SE on the i5-4570 (HD 4600), with V9XTRACE snapshots around it.
# Never call `windows -Controls` without -Title here: it dumps the wifi key.
$c = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$h = '10.0.1.254'
$tools = 'C:\V9XREL'
New-Item -ItemType Directory -Force $OutDir | Out-Null
function Log([string]$t) { $line = "{0:HH:mm:ss} {1}" -f (Get-Date), $t; $line; Add-Content -LiteralPath (Join-Path $OutDir 'run.log') $line }
function Snap([string]$name) {
    & $c exec -Host $h -Application "$tools\V9XTRACE.EXE" -WorkingDirectory $tools -WaitForExit -WaitSeconds 120 | Out-Null
    $latest = (& $c cat -Host $h -Path 'C:\V9XDIAG\V9XSNAL.TXT' -Json 2>$null | ConvertFrom-Json).Text -split "`r?`n"
    $p = @($latest | Where-Object { $_.Trim() })[0]
    if ($p) { & $c get -Host $h -Source $p.Trim() -Destination (Join-Path $OutDir $name) | Out-Null }
}
function Windows3d { $w = & $c windows -Host $h -Json 2>$null | ConvertFrom-Json; if ($w) { @($w.Windows | Where-Object { $_.Title }) } else { $null } }

& $c close-window -Host $h -Title 'NETGEAR' 2>$null | Out-Null
Snap 'snapshot-before.ini'
if ($Grab) { & $c exec -Host $h -Application "$tools\V9XTRACE.EXE" -Arguments $GrabArg -WorkingDirectory $tools -WaitForExit -WaitSeconds 120 | Out-Null }
& $c exec -Host $h -Application 'C:\Program Files\MadOnion.com\3DMark2001 SE\3DMark2001SE.exe' -WorkingDirectory 'C:\Program Files\MadOnion.com\3DMark2001 SE' -Detach | Out-Null
$m = & $c wait-window -Host $h -Title '3DMark2001' -WaitSeconds 180 -Json | ConvertFrom-Json
if (-not $m.Success) { Log 'menu never opened'; exit 2 }
Start-Sleep -Seconds 5
$started = $false
for ($try = 0; $try -lt 6 -and -not $started; ++$try) {
    & $c close-window -Host $h -Title 'Mprexe' 2>$null | Out-Null
    & $c input -Host $h -Sequence 'move 463,424; click left' | Out-Null
    for ($look = 0; $look -lt 3 -and -not $started; ++$look) {
        & $c wait-window -Host $h -Title '3DMark - Display Window' -WaitSeconds 5 2>$null | Out-Null
        $w = Windows3d
        $started = ($w -ne $null -and -not @($w | Where-Object { $_.Class -eq '#32770' -and $_.Title -clike '3DMark2001*' }))
    }
}
if (-not $started) { & $c screenshot -Host $h -OutFile (Join-Path $OutDir 'stuck.png') -TimeoutSeconds 240 | Out-Null; Log 'start input not taken'; exit 3 }
Log "started after $try input(s)"
$began = Get-Date; $lastFlips = $null; $moved = Get-Date; $result = 'running'
while ($true) {
    $min = ((Get-Date) - $began).TotalMinutes
    if ($min -gt $Minutes) { $result = "no score after $Minutes min"; break }
    $s = & $c wait-window -Host $h -Title 'Overall Score' -WaitSeconds 20 -Json 2>$null | ConvertFrom-Json
    if ($s -and $s.Success) { $result = ('score after {0:N1} min' -f $min); break }
    & $c exec -Host $h -Application "$tools\V9XTRACE.EXE" -Arguments '-beat' -WorkingDirectory $tools -WaitForExit -WaitSeconds 30 2>$null | Out-Null
    $beat = (& $c cat -Host $h -Path 'C:\V9XDIAG\V9XBEAT.INI' -Json 2>$null | ConvertFrom-Json).Text -split "`r?`n"
    $flips = @($beat | Where-Object { $_ -match '^FlipHandled=\d+' } | ForEach-Object { ($_ -replace 'FlipHandled=', '').Trim() })[0]
    if ($flips -and $flips -ne $lastFlips) { $lastFlips = $flips; $moved = Get-Date }
    Log ("+{0:N1} min FlipHandled {1}" -f $min, $lastFlips)
    if (((Get-Date) - $moved).TotalSeconds -gt 45 -and -not $stallSnap) { $stallSnap = $true; Log "stalled: mid-stall snapshot"; Snap "snapshot-stall1.ini"; Start-Sleep -Seconds 20; Snap "snapshot-stall2.ini" }
    if (((Get-Date) - $moved).TotalSeconds -gt 180) { $result = "frozen at FlipHandled $lastFlips"; break }
    $ps = & $c ps -Host $h -Json 2>$null | ConvertFrom-Json
    if ($ps -and -not @($ps.Processes | Where-Object { $_.Name -eq '3DMARK2001SE.EXE' })) { $result = '3DMark process gone'; break }
    $w = Windows3d
    if ($w) {
        $foreign = @($w | Where-Object { $_.Class -eq '#32770' -and $_.Title -cnotlike '3DMark2001*' })
        if ($foreign) { $result = "dialog '$($foreign[0].Title)'"; break }
    }
}
Log "RESULT: $result"
& $c windows -Host $h -Title 'Overall Score' -Controls -Json 2>$null | Set-Content -LiteralPath (Join-Path $OutDir 'score-window.json')
& $c kill -Host $h -Name '3DMARK2001SE.EXE' 2>$null | Out-Null
Snap 'snapshot-after.ini'
if ($Grab) { & $c pull-tree -Host $h -Source 'C:\V9XDIAG' -Destination $OutDir -Filter 'V9XGRAB?.BMP' 2>$null | Out-Null }
Log 'done'
