[CmdletBinding()]
param(
    # V9XPROF.TXT and V9XPROF.BIN fetched from the guest, side by side.
    [Parameter(Mandatory)][string]$Text,
    [string]$Binary = ([IO.Path]::ChangeExtension($Text, '.BIN')),
    # The V9XHAL.DLL map of the build that ran, for objects and symbols.
    [string]$HalMap,
    # Only samples in [FromMs, ToMs) after the program started.
    [int]$FromMs = 0,
    [int]$ToMs = [int]::MaxValue,
    # Also print one line per window of this many ms: its top modules.
    [int]$TimelineMs = 0,
    [int]$Top = 25
)
# Resolves V9XPROF.EXE's samples (tools\diag\profile_win32.c) to modules,
# 16-bit code selectors, and inside V9XHAL.DLL to object files and the
# nearest public symbol.
$ErrorActionPreference = 'Stop'

$modules = @()
$selectors = @{}
$flatCs = 0
foreach ($line in Get-Content -LiteralPath $Text) {
    if ($line -match '^module (\d+) ([0-9A-F]{8}) ([0-9A-F]{8}) (.+)$') {
        $modules += [pscustomobject]@{
            Base = [Convert]::ToUInt32($Matches[2], 16)
            Size = [Convert]::ToUInt32($Matches[3], 16)
            Name = [IO.Path]::GetFileName($Matches[4]).ToUpperInvariant()
        }
    } elseif ($line -match '^selector ([0-9A-F]{4}) ([0-9A-F]{8}) ([0-9A-F]{8})') {
        $selectors[[Convert]::ToUInt32($Matches[1], 16)] = $Matches[2]
    } elseif ($line -match '^flatcs ([0-9A-F]{4})') {
        $flatCs = [Convert]::ToUInt32($Matches[1], 16)
    }
}

# The map: each public symbol's address and the object it is in; the image's
# preferred base is its first code segment less 0x1000.
$symbols = New-Object System.Collections.Generic.List[object]
$preferred = 0
if ($HalMap) {
    $object = ''
    foreach ($line in Get-Content -LiteralPath $HalMap) {
        if ($line -match '^_TEXT\s+CODE\s+AUTO\s+([0-9a-f]{8})') {
            $preferred = [Convert]::ToUInt32($Matches[1], 16) - 0x1000
        } elseif ($line -match '^Module: .*\\([^\\(]+)\.obj\(') {
            $object = $Matches[1]
        } elseif ($line -match '^([0-9a-f]{8})[*+ ]\s+(\S+)') {
            $symbols.Add([pscustomobject]@{
                Address = [Convert]::ToUInt32($Matches[1], 16)
                Name = $Matches[2]; Object = $object })
        }
    }
    $symbols = @($symbols | Sort-Object Address)
}
function Find-Symbol([uint32]$address) {
    $lo = 0; $hi = $symbols.Count - 1; $found = $null
    while ($lo -le $hi) {
        $mid = [int](($lo + $hi) / 2)
        if ($symbols[$mid].Address -le $address) { $found = $symbols[$mid]; $lo = $mid + 1 } else { $hi = $mid - 1 }
    }
    $found
}

$bytes = [IO.File]::ReadAllBytes($Binary)
$count = [int]($bytes.Length / 12)
$byModule = @{}; $byObject = @{}; $bySymbol = @{}; $windows = @{}
$taken = 0
for ($i = 0; $i -lt $count; $i++) {
    $tick = [BitConverter]::ToUInt32($bytes, $i * 12)
    if ($tick -lt $FromMs -or $tick -ge $ToMs) { continue }
    $eip = [BitConverter]::ToUInt32($bytes, $i * 12 + 4)
    $cs = [BitConverter]::ToUInt32($bytes, $i * 12 + 8)
    $taken++
    if ($cs -ne $flatCs) {
        $base = $selectors[$cs]
        $name = "16-bit cs=$('{0:X4}' -f $cs) base=$base"
    } else {
        $name = 'unknown'
        foreach ($m in $modules) {
            if ($eip -ge $m.Base -and $eip -lt $m.Base + $m.Size) { $name = $m.Name; $hal = $m; break }
        }
        if ($name -eq 'V9XHAL.DLL' -and $symbols.Count) {
            $s = Find-Symbol ([uint32]($eip - $hal.Base + $preferred))
            if ($s) {
                $byObject[$s.Object] = 1 + [int]$byObject[$s.Object]
                $key = "$($s.Object)!$($s.Name)"
                $bySymbol[$key] = 1 + [int]$bySymbol[$key]
            }
        }
    }
    $byModule[$name] = 1 + [int]$byModule[$name]
    if ($TimelineMs -gt 0) {
        $w = [int][math]::Floor($tick / $TimelineMs)
        if (-not $windows.ContainsKey($w)) { $windows[$w] = @{} }
        $windows[$w][$name] = 1 + [int]$windows[$w][$name]
    }
}

function Show-Share($table, $title) {
    "`n$title"
    $table.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First $Top |
        ForEach-Object { '{0,6:N1}%  {1,8}  {2}' -f (100.0 * $_.Value / $taken), $_.Value, $_.Key }
}
"Samples $taken of $count in [$FromMs, $ToMs) ms"
Show-Share $byModule 'By module:'
if ($byObject.Count) {
    Show-Share $byObject 'V9XHAL.DLL by object (share of all samples):'
    Show-Share $bySymbol 'V9XHAL.DLL by nearest public symbol:'
}
if ($TimelineMs -gt 0) {
    "`nTimeline ($TimelineMs ms windows): top three modules"
    foreach ($w in ($windows.Keys | Sort-Object)) {
        $total = ($windows[$w].Values | Measure-Object -Sum).Sum
        $top3 = $windows[$w].GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 3 |
            ForEach-Object { '{0} {1:N0}%' -f $_.Key, (100.0 * $_.Value / $total) }
        '{0,7:N1}s  {1}' -f ($w * $TimelineMs / 1000.0), ($top3 -join ', ')
    }
}
