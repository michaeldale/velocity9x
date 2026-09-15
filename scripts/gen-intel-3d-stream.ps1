# Render the Intel Phase 4 and Phase 5 arm tables from the compiled builders.
#
# The problem this solves, stated plainly: the Phase 4 execution CRC existed in
# three places - the C builder the driver runs, a literal in
# src\minivdd32\loader.asm the mini-VDD refuses to execute without, and a
# PowerShell reimplementation in check-intel-ring-plan.ps1 - and nothing
# compared them. The Phase 5 layout move found the second and third stale at
# once (docs\decisions\2026-09-15-intel-phase5-layout-move.md). Phase 5's stream
# is roughly six times larger, so maintaining it by hand is not a plan.
#
# So the host test binary emits the streams from the same code the driver
# builds them with (--emit-intel-3d-stream), and this renders two checked-in
# artefacts from that output:
#
#   src\minivdd32\i9xx3d.inc          MASM tables, CRCs and the ring start,
#                                     included by loader.asm.
#   scripts\data\intel-3d-stream.psd1 the same numbers for both validators.
#
# Both are checked in so a netbook trip is reproducible from a clean checkout
# and a human can read the gate. -Verify regenerates and diffs byte for byte,
# which is what run-checks calls.
[CmdletBinding()]
param(
    # Regenerate into a temporary directory and compare, changing nothing.
    [switch]$Verify,
    # Skip building the host binary and use whatever is already there.
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

$incPath = Join-Path $repoRoot 'src\minivdd32\i9xx3d.inc'
$dataPath = Join-Path $repoRoot 'scripts\data\intel-3d-stream.psd1'
$hostBinary = Join-Path $repoRoot 'build\host\v9x-host-tests.exe'

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-host.ps1') | Out-Null
}
if (-not (Test-Path -LiteralPath $hostBinary)) {
    throw ("The host test binary is missing at $hostBinary. It is the source " +
           'of truth for these tables; run build-host.ps1 first.')
}

$emitted = & $hostBinary '--emit-intel-3d-stream'
if ($LASTEXITCODE -ne 0) {
    throw "The host binary refused to emit the Intel 3D stream (exit $LASTEXITCODE)."
}

# One KEY=VALUE per line, hex without a prefix. Deliberately trivial to parse:
# the builders are the clever part and this should not be.
$values = @{}
foreach ($line in $emitted) {
    if ($line -match '^([A-Z0-9]+)=([0-9A-F]+)$') {
        $values[$Matches[1]] = $Matches[2]
    }
}
foreach ($required in @('SCHEMA', 'RESERVEOFFSET', 'RINGSTART', 'SCRATCHOFFSET',
                        'TARGETOFFSET', 'TARGETPITCH', 'TARGETBYTES',
                        'GUARDUPPER', 'FILLWORD', 'TRICOLOR',
                        'P4COUNT', 'P4PACKETCRC', 'P4CRC', 'P5COUNT', 'P5CRC',
                        'COMBINEDCRC')) {
    if (-not $values.ContainsKey($required)) {
        throw "The emitted Intel 3D stream is missing $required."
    }
}
if ($values['SCHEMA'] -ne '1') {
    throw "Unknown emitted-stream schema $($values['SCHEMA'])."
}

function Get-V9xEmittedTable {
    param([string]$Prefix, [int]$Count)
    $table = New-Object 'string[]' $Count
    for ($index = 0; $index -lt $Count; ++$index) {
        $key = '{0}{1:X4}' -f $Prefix, $index
        if (-not $values.ContainsKey($key)) {
            throw "The emitted Intel 3D stream is missing $key."
        }
        $table[$index] = $values[$key]
    }
    return $table
}

$p4Count = [Convert]::ToInt32($values['P4COUNT'], 16)
$p5Count = [Convert]::ToInt32($values['P5COUNT'], 16)
$p4 = Get-V9xEmittedTable -Prefix 'P4' -Count $p4Count
$p5 = Get-V9xEmittedTable -Prefix 'P5' -Count $p5Count

# CRC-32 over the parsed table, recomputed here so the rendered file carries a
# value this script agrees with rather than one it merely copied through.
function Get-V9xCrc32OverHex {
    param([string[]]$Hex)
    [uint64]$crc = 0xffffffffL
    foreach ($text in $Hex) {
        [uint64]$value = [Convert]::ToUInt32($text, 16)
        for ($byte = 0; $byte -lt 4; ++$byte) {
            $crc = $crc -bxor ($value -band 0xffL)
            for ($bit = 0; $bit -lt 8; ++$bit) {
                if ($crc -band 1L) {
                    $crc = (($crc -shr 1) -bxor 0xedb88320L)
                } else {
                    $crc = $crc -shr 1
                }
            }
            $value = $value -shr 8
        }
    }
    return ('{0:X8}' -f [uint32]($crc -bxor 0xffffffffL))
}

$p5Recomputed = Get-V9xCrc32OverHex -Hex $p5
if ($p5Recomputed -ne $values['P5CRC']) {
    throw ("The Phase 5 CRC this script computes ($p5Recomputed) disagrees " +
           "with the one the compiled builder emitted ($($values['P5CRC'])). " +
           'One of the two implementations is wrong; do not render a table ' +
           'until they agree.')
}

$banner = @(
    '; GENERATED FILE - DO NOT EDIT.',
    ';',
    '; Rendered by scripts\gen-intel-3d-stream.ps1 from the compiled builders in',
    '; src\chipsets\intel, through the host test binary''s --emit-intel-3d-stream.',
    '; Edit those builders and regenerate; a hand edit here is caught by',
    '; check-tree.ps1, which recomputes the CRC over the table below and',
    '; compares it with the EQU literal in this same file.',
    ';',
    '; Phase 4 is the blitter fill replayed before the Phase 5 draw. Phase 5 is',
    '; the triangle. The combined CRC covers both in execution order, which is',
    '; what a chained arm token must carry.'
)

$incLines = New-Object 'System.Collections.Generic.List[string]'
foreach ($line in $banner) { $incLines.Add($line) }
$incLines.Add('')
$incLines.Add(('V9X_I9XX_RESERVE_OFFSET EQU 0{0}h' -f $values['RESERVEOFFSET']))
$incLines.Add(('V9X_I9XX_RING_START     EQU 0{0}h' -f $values['RINGSTART']))
$incLines.Add(('V9X_I9XX_SCRATCH_OFFSET EQU 0{0}h' -f $values['SCRATCHOFFSET']))
$incLines.Add(('V9X_I9XX_TARGET_OFFSET  EQU 0{0}h' -f $values['TARGETOFFSET']))
$incLines.Add(('V9X_I9XX_TARGET_PITCH   EQU 0{0}h' -f $values['TARGETPITCH']))
$incLines.Add(('V9X_I9XX_TARGET_BYTES   EQU 0{0}h' -f $values['TARGETBYTES']))
$incLines.Add(('V9X_I9XX_GUARD_UPPER    EQU 0{0}h' -f $values['GUARDUPPER']))
$incLines.Add(('V9X_I9XX_FILL_WORD      EQU 0{0}h' -f $values['FILLWORD']))
$incLines.Add(('V9X_I9XX_TRI_COLOR      EQU 0{0}h' -f $values['TRICOLOR']))
$incLines.Add('')
$incLines.Add(('V9X_I9XX_P4_DWORDS      EQU {0}' -f $p4Count))
$incLines.Add(('V9X_I9XX_P4_PACKET_CRC  EQU 0{0}h' -f $values['P4PACKETCRC']))
$incLines.Add(('V9X_I9XX_P4_CRC         EQU 0{0}h' -f $values['P4CRC']))
$incLines.Add(('V9X_I9XX_P5_DWORDS      EQU {0}' -f $p5Count))
$incLines.Add(('V9X_I9XX_P5_CRC         EQU 0{0}h' -f $values['P5CRC']))
$incLines.Add(('V9X_I9XX_COMBINED_CRC   EQU 0{0}h' -f $values['COMBINEDCRC']))
$incLines.Add('')

function Add-V9xMasmTable {
    param([string]$Label, [string[]]$Hex,
          [System.Collections.Generic.List[string]]$Sink)
    $Sink.Add(("{0} LABEL DWORD" -f $Label))
    for ($index = 0; $index -lt $Hex.Count; $index += 4) {
        $chunk = @()
        for ($offset = 0; $offset -lt 4 -and ($index + $offset) -lt $Hex.Count; ++$offset) {
            $chunk += ('0{0}h' -f $Hex[$index + $offset])
        }
        $Sink.Add('    dd ' + ($chunk -join ', '))
    }
}

Add-V9xMasmTable -Label 'V9xI9xxPhase4Table' -Hex $p4 -Sink $incLines
$incLines.Add('')
Add-V9xMasmTable -Label 'V9xI9xxPhase5Table' -Hex $p5 -Sink $incLines
$incLines.Add('')

$incText = ($incLines -join "`r`n") + "`r`n"

$dataLines = New-Object 'System.Collections.Generic.List[string]'
$dataLines.Add('# GENERATED FILE - DO NOT EDIT.')
$dataLines.Add('#')
$dataLines.Add('# Rendered by scripts\gen-intel-3d-stream.ps1 from the compiled')
$dataLines.Add('# builders. Consumed by the Intel capture validators so they check')
$dataLines.Add('# the same numbers the mini-VDD was armed with, rather than a')
$dataLines.Add('# reimplementation of them.')
$dataLines.Add('@{')
$dataLines.Add('    SchemaVersion = 1')
foreach ($key in @('RESERVEOFFSET', 'RINGSTART', 'SCRATCHOFFSET',
                   'TARGETOFFSET', 'TARGETPITCH', 'TARGETBYTES',
                   'GUARDUPPER', 'FILLWORD', 'TRICOLOR',
                   'P4PACKETCRC', 'P4CRC', 'P5CRC', 'COMBINEDCRC')) {
    $name = (Get-Culture).TextInfo.ToTitleCase($key.ToLower())
    $dataLines.Add(("    {0} = '{1}'" -f $name, $values[$key]))
}
$dataLines.Add(("    Phase4Dwords = {0}" -f $p4Count))
$dataLines.Add(("    Phase5Dwords = {0}" -f $p5Count))
$dataLines.Add("    Phase4Stream = @(")
foreach ($text in $p4) { $dataLines.Add(("        '{0}'" -f $text)) }
$dataLines.Add('    )')
$dataLines.Add("    Phase5Stream = @(")
foreach ($text in $p5) { $dataLines.Add(("        '{0}'" -f $text)) }
$dataLines.Add('    )')
$dataLines.Add('}')

$dataText = ($dataLines -join "`r`n") + "`r`n"

if ($Verify) {
    $problems = @()
    foreach ($pair in @(@{ Path = $incPath; Text = $incText },
                        @{ Path = $dataPath; Text = $dataText })) {
        if (-not (Test-Path -LiteralPath $pair.Path)) {
            $problems += "$($pair.Path) does not exist."
            continue
        }
        $existing = [IO.File]::ReadAllText($pair.Path)
        if ($existing -cne $pair.Text) {
            $problems += ("$($pair.Path) differs from what the compiled " +
                          'builders produce. Run gen-intel-3d-stream.ps1 and ' +
                          'commit the result.')
        }
    }
    if ($problems.Count -ne 0) {
        throw ($problems -join "`n")
    }
    Write-Output ("Intel 3D stream artefacts match the compiled builders " +
                  "(Phase 4 $p4Count dwords, CRC $($values['P4CRC']); " +
                  "Phase 5 $p5Count dwords, CRC $($values['P5CRC']); " +
                  "combined $($values['COMBINEDCRC'])).")
    return
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dataPath) | Out-Null
[IO.File]::WriteAllText($incPath, $incText)
[IO.File]::WriteAllText($dataPath, $dataText)
Write-Output ("Rendered $incPath and $dataPath from the compiled builders " +
              "(Phase 4 $p4Count dwords, CRC $($values['P4CRC']); " +
              "Phase 5 $p5Count dwords, CRC $($values['P5CRC']); " +
              "combined $($values['COMBINEDCRC'])).")
