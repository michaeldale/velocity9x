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

# One KEY=VALUE per line. Deliberately trivial to parse: the builders are the
# clever part and this should not be.
#
# TWO tables, not one, because the values have two shapes. Everything numeric
# is hex without a prefix; probe names are text. A single pattern accepting
# only hex is what silently dropped every NAME= line - the key matched, the
# value did not, so the line vanished and all fifty-two probes reached the
# generated data as Name = ''. Nothing failed, because nothing asked.
#
# Keeping them apart also means a name can never be read as a value or a value
# as a name, whatever either happens to spell.
$values = @{}
$names = @{}
foreach ($line in $emitted) {
    if ($line -match '^([A-Z0-9]+)=([0-9A-F]+)$') {
        $values[$Matches[1]] = $Matches[2]
    } elseif ($line -match '^([A-Z0-9]+NAME)=([A-Za-z][A-Za-z0-9]*)$') {
        $names[$Matches[1]] = $Matches[2]
    } elseif ($line -match '^([A-Z0-9]+)=(.*)$') {
        # Present, and neither shape. Refused rather than skipped: a value the
        # parser does not understand is exactly what this whole comment is
        # about, and the next one should stop the build instead of arriving
        # as an empty string in a committed artefact.
        throw ("The emitted stream line '$line' is neither a hex value nor a " +
               'probe name. Add a shape for it rather than letting it drop.')
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
# The dword the _3DPRIMITIVE starts at. The executor submits the state,
# shader and probe up to here, stops, then submits the primitive - so that
# a drain of the first and a stall on the second distinguishes a dead ring
# from bad packets. It was a literal 50 in loader.asm, correct for the
# 66-dword stream and silently wrong for the 63-dword one.
$incLines.Add(('V9X_I9XX_P5_PRIMITIVE   EQU {0}' -f 
               [Convert]::ToInt32($values['P5PRIM'], 16)))
$incLines.Add(('V9X_I9XX_COMBINED_CRC   EQU 0{0}h' -f $values['COMBINEDCRC']))

# Phase 6 scenes. Three parallel directories rather than a struct array: MASM
# indexes a flat dword table with a scaled index in one instruction, and the
# loader needs each of the three at a different point - the bound before
# staging, the CRC at the execute gate, the table pointer inside the stage
# loop.
$sceneCount = [Convert]::ToInt32($values['SCENECOUNT'], 16)
$incLines.Add('')
$incLines.Add(('V9X_I9XX_SCENE_COUNT    EQU {0}' -f $sceneCount))
$incLines.Add(('V9X_I9XX_SCENE_AUTH     EQU {0}' -f
               [Convert]::ToInt32($values['SCENEAUTHORISED'], 16)))
$incLines.Add(('V9X_I9XX_SCENE_CRC      EQU 0{0}h' -f $values['SCENECOMBINEDCRC']))
$incLines.Add(('V9X_I9XX_SCENE_PROBES   EQU {0}' -f
               [Convert]::ToInt32($values['SCENETOTALPROBES'], 16)))

$sceneDwords = New-Object 'System.Collections.Generic.List[string]'
$sceneCrcs = New-Object 'System.Collections.Generic.List[string]'
$scenePtrs = New-Object 'System.Collections.Generic.List[string]'
$scenePrims = New-Object 'System.Collections.Generic.List[string]'
for ($scene = 0; $scene -lt $sceneCount; ++$scene) {
    $tag = 'SC{0:X4}' -f $scene
    $count = [Convert]::ToInt32($values[($tag + 'COUNT')], 16)
    $sceneDwords.Add(('0{0:X8}h' -f $count))
    $sceneCrcs.Add(('0{0}h' -f $values[($tag + 'CRC')]))
    $scenePtrs.Add(('OFFSET32 V9xI9xxScene{0}Table' -f $scene))
    $scenePrims.Add(('0{0:X8}h' -f 
                     [Convert]::ToInt32($values[($tag + 'PRIM')], 16)))

    $incLines.Add('')
    $incLines.Add(('V9xI9xxScene{0}Table LABEL DWORD' -f $scene))
    $row = New-Object 'System.Collections.Generic.List[string]'
    for ($index = 0; $index -lt $count; ++$index) {
        $key = '{0}{1:X4}' -f $tag, $index
        if (-not $values.ContainsKey($key)) {
            throw "The emitted scene table is missing $key."
        }
        $row.Add(('0{0}h' -f $values[$key]))
        if ($row.Count -eq 4) {
            $incLines.Add('    dd ' + ($row -join ', '))
            $row.Clear()
        }
    }
    if ($row.Count -ne 0) { $incLines.Add('    dd ' + ($row -join ', ')) }
}
$incLines.Add('')
$incLines.Add('V9xI9xxSceneDwords LABEL DWORD')
$incLines.Add('    dd ' + ($sceneDwords -join ', '))
$incLines.Add('V9xI9xxSceneCrc LABEL DWORD')
$incLines.Add('    dd ' + ($sceneCrcs -join ', '))
$incLines.Add('V9xI9xxSceneTables LABEL DWORD')
$incLines.Add('    dd ' + ($scenePtrs -join ', '))
$incLines.Add('V9xI9xxScenePrim LABEL DWORD')
$incLines.Add('    dd ' + ($scenePrims -join ', '))
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
$dataLines.Add('    SchemaVersion = 3')
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

# The software reference: what src\display32\d3d\d3d_raster.c produces for the
# same triangle, run host-side. It is the rasteriser this project already has,
# already host-tested, and already sampling at pixel centres - the same
# convention the hardware's DSTORG half-pixel bias selects, which is the whole
# reason a software reference can agree with this hardware.
#
# The validator REPORTS a disagreement rather than failing on it, until a
# golden is promoted. A one-pixel band along the edges is licensed to differ,
# because the packet audit did not establish the hardware's fill rule.
if ($values.ContainsKey('REFERROR')) {
    throw "The software reference refused to rasterise: $($values['REFERROR'])."
}
foreach ($key in @('REFFILL', 'REFCOLOR', 'REFICOLOR', 'P5PRIM')) {
    if (-not $values.ContainsKey($key)) {
        throw "The emitted stream is missing $key."
    }
}
$dataLines.Add(("    ReferenceFill = '{0}'" -f $values['REFFILL']))
$dataLines.Add(("    ReferenceColor = '{0}'" -f $values['REFCOLOR']))
# The same triangle under the conversion the Intel colour backend was MEASURED
# to use, 2026-09-15. Published alongside the software reference rather than
# replacing it: the two differ by up to one level per channel, that difference
# is understood and documented, and collapsing them would throw away the only
# thing that distinguishes "the known conversion difference" from "the hardware
# has changed".
$dataLines.Add(("    IntelReferenceColor = '{0}'" -f $values['REFICOLOR']))

# The scene table, for the capture validator. It needs the probe coordinates
# and expectations as well as the streams: a probe reading the fill is a
# result in one scene and a regression in another, and only the expectation
# published with it says which.
$dataLines.Add(('    SceneCount = {0}' -f $sceneCount))
$dataLines.Add(('    SceneAuthorisedDraws = {0}' -f
                [Convert]::ToInt32($values['SCENEAUTHORISED'], 16)))
$dataLines.Add(("    SceneCombinedCrc = '{0}'" -f $values['SCENECOMBINEDCRC']))
$dataLines.Add(('    SceneTotalProbes = {0}' -f
                [Convert]::ToInt32($values['SCENETOTALPROBES'], 16)))
$dataLines.Add('    Scenes = @(')
for ($scene = 0; $scene -lt $sceneCount; ++$scene) {
    $tag = 'SC{0:X4}' -f $scene
    $count = [Convert]::ToInt32($values[($tag + 'COUNT')], 16)
    $probes = [Convert]::ToInt32($values[($tag + 'PROBES')], 16)
    $dataLines.Add('        @{')
    $dataLines.Add(('            Id = {0}' -f
                    [Convert]::ToInt32($values[($tag + 'ID')], 16)))
    $dataLines.Add(('            Dwords = {0}' -f $count))
    $dataLines.Add(("            Crc = '{0}'" -f $values[($tag + 'CRC')]))
    $dataLines.Add(('            PrimitiveOffset = {0}' -f
                    [Convert]::ToInt32($values[($tag + 'PRIM')], 16)))
    $dataLines.Add(('            Triangles = {0}' -f
                    [Convert]::ToInt32($values[($tag + 'TRIS')], 16)))
    $dataLines.Add('            Probes = @(')
    for ($probe = 0; $probe -lt $probes; ++$probe) {
        $ptag = '{0}P{1:X4}' -f $tag, $probe
        if (-not $names.ContainsKey($ptag + 'NAME')) {
            throw ("The emitted stream has no name for probe $probe of scene " +
                   "$scene. Every probe is named, and the capture keys are " +
                   'those names - an unnamed probe is unreadable evidence.')
        }
        $dataLines.Add(("                @{{ Name = '{0}'; X = {1}; Y = {2}; Expect = {3} }}" -f
                        $names[($ptag + 'NAME')],
                        [Convert]::ToInt32($values[($ptag + 'X')], 16),
                        [Convert]::ToInt32($values[($ptag + 'Y')], 16),
                        [Convert]::ToInt32($values[($ptag + 'EXPECT')], 16)))
    }
    $dataLines.Add('            )')
    $dataLines.Add('        }')
}
$dataLines.Add('    )')
$dataLines.Add('    ReferencePixels = @(')
for ($index = 0; $index -lt 14; ++$index) {
    $key = 'REFPX{0:X4}' -f $index
    if (-not $values.ContainsKey($key)) {
        throw "The emitted stream is missing $key."
    }
    $dataLines.Add(("        '{0}'" -f $values[$key]))
}
$dataLines.Add('    )')
# Coverage from the rasteriser, colour from the measurement. Every probe is
# required, for the reason the validator gives about partial probe sets: a
# reference that silently carries thirteen of fourteen would let a capture
# missing one look complete.
$dataLines.Add('    IntelReferencePixels = @(')
for ($index = 0; $index -lt 14; ++$index) {
    $key = 'REFIPX{0:X4}' -f $index
    if (-not $values.ContainsKey($key)) {
        throw "The emitted stream is missing $key."
    }
    $dataLines.Add(("        '{0}'" -f $values[$key]))
}
$dataLines.Add('    )')
$dataLines.Add('    ReferenceRows = @(')
for ($index = 0; $index -lt 480; ++$index) {
    $key = 'REFR{0:X4}' -f $index
    if (-not $values.ContainsKey($key)) {
        throw "The emitted stream is missing $key."
    }
    $dataLines.Add(("        '{0}'" -f $values[$key]))
}
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
