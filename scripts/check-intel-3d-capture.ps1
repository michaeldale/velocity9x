# Validate an INTEL3D0.TXT capture from the Intel Phase 5 sequencer.
#
# The capture is the artefact; this is what makes reading it a check rather
# than an impression. It asserts the invariants that hold whatever the hardware
# did, and reports - rather than fails - the things that are observations.
#
# Deliberately separate from check-intel-ring-plan.ps1: Phase 4 and Phase 5
# have disjoint step numbers, disjoint refusal spaces and separate captures,
# and one validator that accepted either would defeat that.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'File')]
    [string]$Path,
    [Parameter(Mandatory = $true, ParameterSetName = 'SelfTest')]
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

# The driver's probe table length. Asserted against, never read from the
# capture: a count a capture supplies can only prove the capture is consistent
# with itself. Kept in step with v9x_p5_probes[] in src\display16\intel_3d16.c.
$script:V9xExpectedProbes = 14
# Bound on the unarmed sample set BEFORE anything loops over it, so a capture
# cannot drive the checker with its own number.
$script:V9xMaxSamples = 128
$repoRoot = Split-Path -Parent $PSScriptRoot
$generatedPath = Join-Path $repoRoot 'scripts\data\intel-3d-stream.psd1'

function ConvertFrom-V9x3dIni {
    param([string[]]$Lines)
    $values = @{}
    foreach ($line in $Lines) {
        if ($line -match '^\s*\[' -or $line -match '^\s*$') { continue }
        if ($line -notmatch '^([A-Za-z0-9_]+)=(.*)$') {
            throw "INTEL3D0.TXT has an unparsable line: $line"
        }
        $values[$Matches[1]] = $Matches[2]
    }
    return $values
}

function Get-V9x3dHex32 {
    param([hashtable]$Values, [string]$Key)
    if (-not $Values.ContainsKey($Key)) {
        throw "INTEL3D0.TXT is missing $Key."
    }
    if ($Values[$Key] -notmatch '^[0-9A-F]{8}$') {
        throw "INTEL3D0.TXT key $Key is not eight hex digits: $($Values[$Key])."
    }
    return [Convert]::ToUInt32($Values[$Key], 16)
}

function Test-V9xIntel3dCapture {
    param([string[]]$Lines)

    $values = ConvertFrom-V9x3dIni -Lines $Lines
    if (-not (Test-Path -LiteralPath $generatedPath)) {
        throw ('scripts\data\intel-3d-stream.psd1 is missing; it is what the ' +
               'capture is checked against.')
    }
    $generated = Import-PowerShellDataFile -LiteralPath $generatedPath

    foreach ($required in @('SchemaVersion', 'Access', 'CaptureBuildId',
                            'Result', 'StreamDwords', 'StreamCrc',
                            'GeneratedCrc', 'Precondition')) {
        if (-not $values.ContainsKey($required)) {
            throw "INTEL3D0.TXT is missing $required."
        }
    }
    if ($values['SchemaVersion'] -ne '1') {
        throw "Unknown INTEL3D0.TXT schema $($values['SchemaVersion'])."
    }

    # The stream the driver built must be the stream that was generated. If
    # these differ, what would run is not what was reviewed - which is the
    # whole reason the driver recomputes and compares them itself.
    $streamDwords = Get-V9x3dHex32 -Values $values -Key 'StreamDwords'
    if ($streamDwords -ne $generated.Phase5Dwords) {
        throw ("INTEL3D0.TXT reports $streamDwords stream dwords but the " +
               "generated table has $($generated.Phase5Dwords).")
    }
    for ($index = 0; $index -lt $streamDwords; ++$index) {
        $key = 'S{0:X4}' -f $index
        $actual = Get-V9x3dHex32 -Values $values -Key $key
        $expected = [Convert]::ToUInt32($generated.Phase5Stream[$index], 16)
        if ($actual -ne $expected) {
            throw ("INTEL3D0.TXT stream dword $index is " +
                   ('{0:X8}' -f $actual) + ' but the generated table says ' +
                   ('{0:X8}' -f $expected) + '.')
        }
    }
    $streamCrc = Get-V9x3dHex32 -Values $values -Key 'StreamCrc'
    $generatedCrc = Get-V9x3dHex32 -Values $values -Key 'GeneratedCrc'
    if ($streamCrc -ne $generatedCrc) {
        throw ('INTEL3D0.TXT StreamCrc and GeneratedCrc differ, so the driver ' +
               'built something other than the reviewed stream.')
    }
    if (('{0:X8}' -f $streamCrc) -ne $generated.P5Crc) {
        throw ("INTEL3D0.TXT StreamCrc is " + ('{0:X8}' -f $streamCrc) +
               " but the generated Phase 5 CRC is $($generated.P5Crc).")
    }

    # The vertices are reported twice on purpose. If the raw bits and the
    # decoded integers disagree, the float transport is wrong - and that shows
    # here rather than as a wrong picture.
    $expectedX = @(160, 480, 320)
    $expectedY = @(120, 120, 400)
    for ($vertex = 0; $vertex -lt 3; ++$vertex) {
        $xKey = 'VD{0:X4}' -f ($vertex * 8)
        $yKey = 'VD{0:X4}' -f ($vertex * 8 + 1)
        $zKey = 'VD{0:X4}' -f ($vertex * 8 + 2)
        $wKey = 'VD{0:X4}' -f ($vertex * 8 + 3)
        if ((Get-V9x3dHex32 -Values $values -Key $xKey) -ne $expectedX[$vertex] -or
            (Get-V9x3dHex32 -Values $values -Key $yKey) -ne $expectedY[$vertex]) {
            throw ("INTEL3D0.TXT vertex $vertex decodes to the wrong screen " +
                   'position; the float transport disagrees with the geometry.')
        }
        if ((Get-V9x3dHex32 -Values $values -Key $zKey) -ne 0) {
            throw "INTEL3D0.TXT vertex $vertex has non-zero Z; Phase 5 is un-Z'd."
        }
        if ((Get-V9x3dHex32 -Values $values -Key $wKey) -ne 1) {
            throw ("INTEL3D0.TXT vertex $vertex has W other than one, so the " +
                   'perspective divide is not the identity.')
        }
        $colourKey = 'VC{0:X4}' -f $vertex
        if ((Get-V9x3dHex32 -Values $values -Key $colourKey) -ne
                [Convert]::ToUInt32($generated.Tricolor, 16)) {
            throw ("INTEL3D0.TXT vertex $vertex carries a different colour; " +
                   'all three must match or the shading mode stops being moot.')
        }
    }

    # The layout the capture describes must be the one that was generated.
    foreach ($pair in @(@{ Key = 'RefTargetOffset'; Gen = 'Targetoffset' },
                        @{ Key = 'RefTargetPitch';  Gen = 'Targetpitch' },
                        @{ Key = 'RefTargetBytes';  Gen = 'Targetbytes' },
                        @{ Key = 'RefGuardUpper';   Gen = 'Guardupper' })) {
        $actual = '{0:X8}' -f (Get-V9x3dHex32 -Values $values -Key $pair.Key)
        if ($actual -ne $generated[$pair.Gen]) {
            throw ("INTEL3D0.TXT $($pair.Key) is $actual but the generated " +
                   "layout says $($generated[$pair.Gen]).")
        }
    }

    # The reserve must lie inside the measured backed prefix, re-derived by the
    # driver on this boot rather than inherited from the Phase 2 capture.
    $first = Get-V9x3dHex32 -Values $values -Key 'P5RefReserveFirst'
    $count = Get-V9x3dHex32 -Values $values -Key 'P5RefReserveCount'
    $prefix = Get-V9x3dHex32 -Values $values -Key 'P5RefBackedPrefix'
    if ($count -eq 0 -or ($first + $count) -gt $prefix) {
        throw ("INTEL3D0.TXT reports the reserve outside the measured backed " +
               "prefix: first $first, count $count, prefix $prefix.")
    }

    $armed = $values['Access'] -eq 'armed-one-shot'
    $result = $values['Result']

    if (-not $armed) {
        # An unarmed capture must have written nothing and must still prove
        # the two things B1 needs: a stable read-only hash, and a live
        # address path.
        if ($result -ne 'NO-WRITE') {
            throw ("An unarmed INTEL3D0.TXT must report Result=NO-WRITE, not " +
                   "$result.")
        }
        foreach ($forbidden in @('FillHashA', 'DrawHashA', 'HeapProbeAfter')) {
            if ($values.ContainsKey($forbidden)) {
                throw ("An unarmed INTEL3D0.TXT must not contain $forbidden; " +
                       'its presence means the no-write path wrote.')
            }
        }
        # The bulk hash is gone and its absence must be DECLARED, not merely
        # observed: a capture that simply lacks the key is indistinguishable
        # from one whose hash ran and was lost to a lock.
        if (-not $values.ContainsKey('HashOmitted')) {
            throw ('An unarmed INTEL3D0.TXT must carry HashOmitted saying why ' +
                   'the full-target hash was not taken. See the decision ' +
                   'record 2026-09-15-bulk-aperture-reads-hang-the-945gse.md.')
        }
        # The sample set that replaced it. Note carefully what this does and
        # does not establish: each point returning the same value twice is
        # SAMPLE stability. It says nothing about the rest of the target, and
        # the keys are named so a reader cannot mistake one for the other.
        $sampleCount = Get-V9x3dHex32 -Values $values -Key 'SampleCount'
        if ($sampleCount -lt 1) {
            throw ('An unarmed INTEL3D0.TXT must report SampleCount. Without ' +
                   'the sample set a no-write boot proves nothing at all ' +
                   'about the address path.')
        }
        # Bounded before the loop, not after it. An unbounded count out of the
        # capture decides how long this checker runs.
        if ($sampleCount -gt $script:V9xMaxSamples) {
            throw ("An unarmed INTEL3D0.TXT claims $sampleCount samples; the " +
                   "bound is $($script:V9xMaxSamples). The no-write path is " +
                   'bounded deliberately.')
        }
        # Two reads per sample is what makes the pair a stability check at all,
        # so the reported read count must equal it. Without this, eight pairs
        # alongside SampleReads=0 passed - the count and the evidence were
        # never compared.
        $sampleReads = Get-V9x3dHex32 -Values $values -Key 'SampleReads'
        if ($sampleReads -ne (2 * $sampleCount)) {
            throw ("SampleReads is $sampleReads for $sampleCount samples; it " +
                   "must be $(2 * $sampleCount). Each sample is read twice, " +
                   'and a read count that does not match the sample set means ' +
                   'one of the two is not describing what happened.')
        }
        for ($index = 0; $index -lt $sampleCount; ++$index) {
            $keyA = 'SA{0:X4}' -f $index
            $keyB = 'SB{0:X4}' -f $index
            if (-not $values.ContainsKey($keyA) -or
                -not $values.ContainsKey($keyB)) {
                throw ("Sample $index is missing its $keyA/$keyB pair. A " +
                       'truncated sample set means the boot stopped part way ' +
                       'through reading the aperture, which is exactly the ' +
                       'failure this capture exists to catch.')
            }
            if ((Get-V9x3dHex32 -Values $values -Key $keyA) -ne
                (Get-V9x3dHex32 -Values $values -Key $keyB)) {
                throw ("Sample $index read two different values. That is a " +
                       'point-level instability at this address; it does not ' +
                       'characterise the target.')
            }
        }
        # An explicit ceiling on how much aperture the no-write path may
        # touch. 16 reads is today's figure and 256 is the ceiling with room to
        # grow; what matters is that a future change cannot quietly walk the
        # count back toward the 307,200 that hard locked the machine without
        # this refusing first. The safe bound is NOT known - see
        # plans\intel-phase5-bounded-readback.md - so this is a guard rail, not
        # a measured limit.
        if ($sampleReads -gt 256) {
            throw ("The unarmed path reports $sampleReads aperture reads. " +
                   'The no-write path is bounded deliberately; a count at ' +
                   'bulk scale is the operation that hard locks this part.')
        }
        if ($values['SampleStable'] -cne '1') {
            throw ('SampleStable is not 1: a sampled address returned two ' +
                   'different values.')
        }
        return [pscustomobject]@{
            Armed = $false; Result = $result; StreamCrc = '{0:X8}' -f $streamCrc
            Notes = @("unarmed: no writes, $sampleCount samples stable " +
                      '(sample stability only, not target stability)')
        }
    }

    # Armed.
    if ((Get-V9x3dHex32 -Values $values -Key 'Phase4Passed') -eq 0) {
        throw ('An armed INTEL3D0.TXT reports Phase 4 did not pass this boot. ' +
               'Phase 5 must not run without it: Phase 4 is the only thing ' +
               'that separates a broken layout from wrong 3D packets.')
    }
    $notes = @()
    # Both bulk read-backs must DECLARE their absence. An armed boot that
    # simply lacks these keys is a boot whose read-back may have been lost to a
    # lock, and the two must not look alike.
    foreach ($omission in @('HashOmitted', 'RowCrcOmitted')) {
        if (-not $values.ContainsKey($omission)) {
            throw ("An armed INTEL3D0.TXT must carry $omission. The " +
                   'full-target hash and the 480 row CRCs were removed on ' +
                   '2026-09-15 after the hash hard locked the netbook; a ' +
                   'capture that is merely silent about them cannot be told ' +
                   'apart from one that attempted them and died.')
        }
    }
    foreach ($forbidden in @('FillHashA', 'DrawHashA', 'R0000')) {
        if ($values.ContainsKey($forbidden)) {
            throw ("An armed INTEL3D0.TXT carries $forbidden, so a bulk " +
                   'read-back has been reintroduced. That is the operation ' +
                   'that hard locks this part - see the decision record ' +
                   'before restoring it.')
        }
    }
    # The fourteen probes are now the WHOLE of the draw evidence, so a missing
    # one cannot pass. Previously the reference comparison skipped absent keys,
    # which meant a capture with no probes at all reported no mismatches.
    # The count is fixed by the driver's probe table, so it is asserted
    # against that constant and never taken from the capture. Looping to a
    # count the file supplies lets a capture declare PixelProbes=1, carry one
    # probe, and satisfy its own claim - which is not evidence of anything.
    $probeCount = Get-V9x3dHex32 -Values $values -Key 'PixelProbes'
    if ($probeCount -ne $script:V9xExpectedProbes) {
        throw ("An armed INTEL3D0.TXT reports $probeCount pixel probes; the " +
               "driver publishes $($script:V9xExpectedProbes). The probes are " +
               'the only draw evidence left, so the set must be complete, not ' +
               'merely self-consistent.')
    }
    for ($index = 0; $index -lt $script:V9xExpectedProbes; ++$index) {
        $key = 'PX{0:X4}' -f $index
        if (-not $values.ContainsKey($key)) {
            throw ("Probe $key is missing. A truncated probe set fails rather " +
                   'than reporting on the ones that happen to be present.')
        }
        # Present but unreadable is the same as absent for this purpose.
        $null = Get-V9x3dHex32 -Values $values -Key $key
    }
    # The software reference, REPORTED and never failed on until a golden is
    # promoted - the plan is explicit about that. The reference is
    # src\display32\d3d\d3d_raster.c run host-side by the generator; it samples
    # at pixel centres, the same convention the hardware's DSTORG half-pixel
    # bias selects, which is why the two can agree at all.
    #
    # A one-pixel band along the triangle's edges is licensed to differ,
    # because the packet audit did not establish the hardware's fill rule. The
    # probes deliberately avoid the edges, so a disagreement at one of THEM is
    # a real difference and worth reading - but it is still reported, because
    # this comparison has never been run against hardware even once.
    if ($generated.ContainsKey('ReferencePixels')) {
        $mismatches = 0
        $compared = 0
        for ($index = 0; $index -lt $script:V9xExpectedProbes; ++$index) {
            $key = 'PX{0:X4}' -f $index
            # No `continue` on an absent key: every probe is required above, so
            # a gap here is a contradiction rather than something to skip. The
            # old skip is what let a capture with no probes report no
            # mismatches and be summarised as full agreement.
            ++$compared
            $actual = Get-V9x3dHex32 -Values $values -Key $key
            $expected = [Convert]::ToUInt32($generated.ReferencePixels[$index], 16)
            # The capture reads a dword - two 16-bit pixels - so compare the
            # half the probe's column selects. Both halves should hold the same
            # value inside a flat-filled region, and comparing the low half is
            # what the even-column probes address.
            if (($actual -band 0xffff) -ne $expected) {
                Write-Warning ("Probe $key reads " +
                               ('{0:X8}' -f $actual) +
                               ' where the software reference says ' +
                               ('{0:X4}' -f $expected) + '.')
                ++$mismatches
            }
        }
        if ($mismatches -ne 0) {
            $notes += ("$mismatches of $compared pixel probes disagree with the " +
                       'software reference. REPORTED, not failed: this ' +
                       'comparison has never run against hardware, and no ' +
                       'golden has been promoted.')
        } else {
            $notes += ("All $compared pixel probes agree with the software " +
                       'reference.')
        }
    }

    # In-reserve guards must be untouched. These are ours, unlike HeapProbe.
    foreach ($pair in @(@{ Before = 'GLow0'; After = 'GLow1'; What = 'lower' },
                        @{ Before = 'GUpp0'; After = 'GUpp1'; What = 'upper' })) {
        if (-not $values.ContainsKey($pair.After)) { continue }
        if ((Get-V9x3dHex32 -Values $values -Key $pair.Before) -ne
            (Get-V9x3dHex32 -Values $values -Key $pair.After)) {
            throw ("The in-reserve $($pair.What) guard changed across the draw. " +
                   'That is an overrun out of the target and is a kill, not an ' +
                   'observation.')
        }
    }
    # HeapProbe is an OBSERVATION, not an assertion. The dword below the
    # reserve is still published heap and nothing here proves it quiescent, so
    # a change is reported and never failed on.
    if ($values.ContainsKey('HeapProbeAfter')) {
        if ((Get-V9x3dHex32 -Values $values -Key 'HeapProbeBefore') -ne
            (Get-V9x3dHex32 -Values $values -Key 'HeapProbeAfter')) {
            $notes += ('HeapProbe changed across the draw. This is an ' +
                       'observation, not corruption: that dword is published ' +
                       'heap and nothing here proves it was ours for the ' +
                       'interval.')
        }
    }
    return [pscustomobject]@{
        Armed = $true; Result = $result; StreamCrc = '{0:X8}' -f $streamCrc
        Notes = $notes
    }
}

if ($SelfTest) {
    $generated = Import-PowerShellDataFile -LiteralPath $generatedPath
    $lines = New-Object 'System.Collections.Generic.List[string]'
    $lines.Add('[Intel3D]')
    $lines.Add('SchemaVersion=1')
    $lines.Add('Access=no-hardware-writes')
    $lines.Add('CaptureBuildId=p5-self-test')
    $lines.Add('Phase4Passed=00000000')
    $lines.Add(('StreamDwords={0:X8}' -f $generated.Phase5Dwords))
    for ($index = 0; $index -lt $generated.Phase5Dwords; ++$index) {
        $lines.Add(('S{0:X4}={1}' -f $index, $generated.Phase5Stream[$index]))
    }
    $lines.Add("StreamCrc=$($generated.P5Crc)")
    $lines.Add("GeneratedCrc=$($generated.P5Crc)")
    foreach ($row in @(@(0, 160), @(1, 120), @(2, 0), @(3, 1),
                       @(8, 480), @(9, 120), @(10, 0), @(11, 1),
                       @(16, 320), @(17, 400), @(18, 0), @(19, 1))) {
        $lines.Add(('VD{0:X4}={1:X8}' -f $row[0], $row[1]))
    }
    for ($vertex = 0; $vertex -lt 3; ++$vertex) {
        $lines.Add(('VC{0:X4}={1}' -f $vertex, $generated.Tricolor))
    }
    $lines.Add("RefTargetOffset=$($generated.Targetoffset)")
    $lines.Add("RefTargetPitch=$($generated.Targetpitch)")
    $lines.Add("RefTargetBytes=$($generated.Targetbytes)")
    $lines.Add("RefGuardUpper=$($generated.Guardupper)")
    $lines.Add('P5RefBackedPrefix=000007B0')
    $lines.Add('P5RefReserveFirst=000006B0')
    $lines.Add('P5RefReserveCount=00000100')
    $lines.Add('Precondition=00000001')
    $lines.Add('HashOmitted=bulk-aperture-read-hang')
    $lines.Add('SampleCount=00000008')
    $lines.Add('SampleReads=00000010')
    for ($sample = 0; $sample -lt 8; ++$sample) {
        $lines.Add(('SA{0:X4}=08420842' -f $sample))
        $lines.Add(('SB{0:X4}=08420842' -f $sample))
    }
    $lines.Add('SampleStable=1')
    $lines.Add('Result=NO-WRITE')

    $null = Test-V9xIntel3dCapture -Lines $lines

    $mutations = @(
        @{ Old = 'SB0003=08420842'; New = 'SB0003=08420843'
           Why = 'a sampled address returned two different values' },
        @{ Old = 'SampleStable=1'; New = 'SampleStable=0'
           Why = 'sample set reported unstable' },
        @{ Old = 'SA0005=08420842'; New = 'SAxxxx=08420842'
           Why = 'a truncated sample set, one pair missing' },
        @{ Old = 'HashOmitted=bulk-aperture-read-hang'; New = 'HashNote=x'
           Why = 'the bulk hash omission is not declared' },
        @{ Old = 'SampleCount=00000008'; New = 'SampleCount=00000000'
           Why = 'no samples taken at all' },
        @{ Old = "StreamCrc=$($generated.P5Crc)"; New = 'StreamCrc=DEADBEEF'
           Why = 'stream CRC not the generated one' },
        @{ Old = 'S0000=' + $generated.Phase5Stream[0]; New = 'S0000=00000000'
           Why = 'stream dword differs from the generated table' },
        @{ Old = 'VD0001=00000078'; New = 'VD0001=00000079'
           Why = 'decoded vertex disagrees with the geometry' },
        @{ Old = 'VD0003=00000001'; New = 'VD0003=00000002'
           Why = 'W is not one' },
        @{ Old = 'Result=NO-WRITE'; New = 'Result=PASS'
           Why = 'unarmed capture claiming a pass' },
        @{ Old = 'P5RefReserveFirst=000006B0'; New = 'P5RefReserveFirst=00000750'
           Why = 'reserve outside the measured backed prefix' },
        @{ Old = 'SampleReads=00000010'; New = 'SampleReads=00025800'
           Why = 'a sample read count back at bulk-hash scale' },
        # Both of these passed b46cb1a. The checker bounded SampleReads from
        # above but never tied it to SampleCount, and it looped to a probe
        # count the capture supplied rather than to the driver's own.
        @{ Old = 'SampleReads=00000010'; New = 'SampleReads=00000000'
           Why = 'eight sample pairs reporting zero reads' },
        @{ Old = 'SampleReads=00000010'; New = 'SampleReads=00000008'
           Why = 'read count is not two per sample' },
        @{ Old = 'SampleCount=00000008'; New = 'SampleCount=00000004'
           Why = 'sample count disagrees with the pairs present' }
    )
    foreach ($mutation in $mutations) {
        $broken = @($lines | ForEach-Object {
            if ($_ -ceq $mutation.Old) { $mutation.New } else { $_ }
        })
        if (($broken -join "`n") -ceq ($lines -join "`n")) {
            throw ("Self-test mutation '$($mutation.Why)' changed nothing; it " +
                   'names a line that is not in the fixture.')
        }
        $rejected = $false
        try { $null = Test-V9xIntel3dCapture -Lines $broken } catch { $rejected = $true }
        if (-not $rejected) {
            throw ("The Intel 3D capture validator accepted a mutation: " +
                   $mutation.Why + '.')
        }
    }
    # ---------------------------------------------------------------------
    # An ARMED fixture, which the self-test did not have at all. Every probe
    # assertion was therefore uncovered, and P1 - a capture declaring
    # PixelProbes=1, carrying one probe, and being summarised as "All 14 pixel
    # probes agree" - passed b46cb1a.
    # ---------------------------------------------------------------------
    $armedLines = New-Object 'System.Collections.Generic.List[string]'
    foreach ($line in $lines) {
        if ($line -clike 'Access=*') { continue }
        if ($line -clike 'Phase4Passed=*') { continue }
        if ($line -clike 'Result=*') { continue }
        if ($line -clike 'HashOmitted=*') { continue }
        if ($line -clike 'Sample*') { continue }
        if ($line -clike 'SA*' -or $line -clike 'SB*') { continue }
        $armedLines.Add($line)
    }
    $armedLines.Add('Access=armed-one-shot')
    $armedLines.Add('Phase4Passed=00000001')
    $armedLines.Add('HashOmitted=bulk-aperture-read-hang')
    $armedLines.Add('RowCrcOmitted=bulk-aperture-read-hang')
    $armedLines.Add('PixelProbes=0000000E')
    # Built from the software reference rather than a flat value, so the clean
    # armed fixture exercises the agreement path and the self-test stays quiet.
    # A self-test that always prints warnings teaches its reader to skip them.
    for ($probe = 0; $probe -lt 14; ++$probe) {
        $expected = if ($generated.ContainsKey('ReferencePixels')) {
            $generated.ReferencePixels[$probe]
        } else { '00000842' }
        $armedLines.Add(('PX{0:X4}={1}' -f $probe, $expected))
    }
    $armedLines.Add('Result=PASS')

    $null = Test-V9xIntel3dCapture -Lines $armedLines

    $armedMutations = @(
        @{ Old = 'PixelProbes=0000000E'; New = 'PixelProbes=00000001'
           Why = 'a probe count the capture chose for itself (P1)' },
        @{ Old = $armedLines[$armedLines.Count - 4]
           New = 'PXzzzz=00000000'
           Why = 'a probe missing from the middle of the set' },
        @{ Old = 'HashOmitted=bulk-aperture-read-hang'; New = 'HashNote=x'
           Why = 'armed capture not declaring the hash omission' },
        @{ Old = 'RowCrcOmitted=bulk-aperture-read-hang'; New = 'RowNote=x'
           Why = 'armed capture not declaring the row-CRC omission' },
        @{ Old = 'Result=PASS'; New = 'Result=PASS
R0000=DEADBEEF'
           Why = 'a bulk row-CRC read-back reintroduced' },
        @{ Old = 'Phase4Passed=00000001'; New = 'Phase4Passed=00000000'
           Why = 'armed run without Phase 4 passing this boot' }
    )
    foreach ($mutation in $armedMutations) {
        $broken = @($armedLines | ForEach-Object {
            if ($_ -ceq $mutation.Old) { $mutation.New } else { $_ }
        })
        if (($broken -join "`n") -ceq ($armedLines -join "`n")) {
            throw ("Self-test mutation '$($mutation.Why)' changed nothing; it " +
                   'names a line that is not in the armed fixture.')
        }
        $rejected = $false
        try { $null = Test-V9xIntel3dCapture -Lines $broken } catch { $rejected = $true }
        if (-not $rejected) {
            throw ('The Intel 3D capture validator accepted an armed ' +
                   "mutation: $($mutation.Why).")
        }
    }

    Write-Output ("Intel 3D capture validator self-test passed (clean unarmed " +
                  "and armed accepted, $($mutations.Count) unarmed and " +
                  "$($armedMutations.Count) armed mutations rejected).")
    return
}

$result = Test-V9xIntel3dCapture -Lines (Get-Content -LiteralPath $Path)
# Notes are observations, not warnings. Emitting "unarmed: no writes, 8 samples
# stable" as a WARNING on a capture that passed is the same mistake as a
# self-test that always prints warnings: it teaches the reader that warnings
# from this tool can be skipped, and the ones that matter - a probe
# disagreeing with the software reference - are genuine Write-Warning calls
# further up.
foreach ($note in $result.Notes) { Write-Output "  note: $note" }
Write-Output ("Intel 3D capture accepted: armed=$($result.Armed), " +
              "result=$($result.Result), stream CRC $($result.StreamCrc).")
