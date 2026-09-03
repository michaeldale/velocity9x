<#
.SYNOPSIS
  Compare two DirectDraw probe result files key by key.

.DESCRIPTION
  The probe (V9XDDP.EXE) writes C:\V9XDIAG\V9XDD.INI on the guest: one
  key=value per line. This script diffs two such files and reports the keys
  that differ, after dropping the keys that change on every run (heap
  pointers, handles, build ids, timings).

  Two ways to use it:

    1. Regression: the same driver before and after a change, on one machine.
       Any difference is the change's doing.

    2. Reference: this driver against the vendor's own driver on the same
       chip, the same probe binary on both. The differences are the work list
       for that chip. Vendor drivers write their target in the display's own
       layout, and so does ours, so raw values compare directly; where a rung
       encodes an expectation in a raw number (a few older ones do), compare
       the *_Ok key and treat the raw as evidence.

  An expectations file (-Expect) lists, per chip, keys whose difference is
  known and accepted, one key per line with an optional "# reason". Those are
  reported under a separate heading so they are not silently lost. See
  docs\probe\README.md for the file layout and where references live.

.EXAMPLE
  .\scripts\compare-probe.ps1 -Left build\probe\virge-native.ini -Right build\probe\virge-v9x.ini
  .\scripts\compare-probe.ps1 -Left a.ini -Right b.ini -Expect docs\probe\expectations\trio3d.txt
#>
param(
    [Parameter(Mandatory = $true)][string]$Left,
    [Parameter(Mandatory = $true)][string]$Right,
    [string]$Expect = '',
    [switch]$IncludeVolatile
)

$ErrorActionPreference = 'Stop'

# Keys that legitimately differ between any two runs.
$volatile = '^(CbRaw|GblRaw|Gbl[A-Za-z]+|TexHandle|Build|CbHalSurfFlags|CbHalDdFlags|CbHelDdFlags)|Ms=$|Ms$|Address$|Handle$'

function Read-Probe([string]$path) {
    $table = @{}
    foreach ($line in Get-Content -LiteralPath $path) {
        if ($line -match '^\s*([^=;#\[]+?)\s*=\s*(.*)$') {
            $table[$matches[1]] = $matches[2]
        }
    }
    return $table
}

$l = Read-Probe $Left
$r = Read-Probe $Right

$expected = @{}
if ($Expect -ne '') {
    foreach ($line in Get-Content -LiteralPath $Expect) {
        $t = $line.Trim()
        if ($t -eq '' -or $t.StartsWith('#')) { continue }
        $parts = $t -split '\s+#\s*', 2
        $expected[$parts[0].Trim()] = if ($parts.Count -gt 1) { $parts[1] } else { '' }
    }
}

$keys = @($l.Keys) + @($r.Keys) | Sort-Object -Unique
$diff = @(); $known = @(); $onlyLeft = @(); $onlyRight = @()
foreach ($k in $keys) {
    if (-not $IncludeVolatile -and ($k -match $volatile)) { continue }
    $inL = $l.ContainsKey($k); $inR = $r.ContainsKey($k)
    if ($inL -and -not $inR) { $onlyLeft += $k; continue }
    if ($inR -and -not $inL) { $onlyRight += $k; continue }
    if ($l[$k] -ne $r[$k]) {
        $row = '{0,-40} {1,-22} {2}' -f $k, $l[$k], $r[$k]
        if ($expected.ContainsKey($k)) { $known += ($row + '   # ' + $expected[$k]) } else { $diff += $row }
    }
}

'{0,-40} {1,-22} {2}' -f 'key', (Split-Path -Leaf $Left), (Split-Path -Leaf $Right)
'---'
if ($diff.Count -eq 0) { '(no unexpected differences)' } else { $diff }
if ($known.Count -gt 0) { ''; '--- differences listed in the expectations file ---'; $known }
if ($onlyLeft.Count -gt 0) { ''; '--- only in left ---'; $onlyLeft }
if ($onlyRight.Count -gt 0) { ''; '--- only in right ---'; $onlyRight }
''
'{0} unexpected, {1} expected, {2} only-left, {3} only-right' -f $diff.Count, $known.Count, $onlyLeft.Count, $onlyRight.Count
