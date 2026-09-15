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
    # Schema 1 is the captures preserved under docs\probe before the error
    # registers were read; schema 2 adds PreErr/PostErr and their completeness
    # status. Both are accepted, and the schema-2 requirements below apply only
    # to schema 2 - a version bump exists so new fields can be REQUIRED without
    # retroactively rejecting evidence already collected.
    # Schema 3 adds the Phase 6 scene sections. The driver stamps it ONLY on a
    # phase-6 capture: emitting it unconditionally made every existing B1 and
    # Phase 5 capture fail here, because they declare fields they do not carry.
    $schema = $values['SchemaVersion']
    if ($schema -ne '1' -and $schema -ne '2' -and $schema -ne '3') {
        throw "Unknown INTEL3D0.TXT schema $schema."
    }
    # A phase-6 capture publishes its evidence PER SCENE and emits none of the
    # phase-5 globals: no single PostErr set, no PX series, no bulk-read
    # omission notes, because there is no single draw for them to describe.
    #
    # Requiring them of it rejected every real phase-6 capture. The self-test
    # did not catch that because its fixture was a phase-5 one with scene
    # sections appended - it INHERITED the fields and so never asked whether
    # they were required. A fixture built by adding to a passing one tests
    # what was added and nothing about what was already there.
    $isScene = ($schema -eq '3')

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
        if ($isScene) { continue }
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
    # The GPU error registers, and whether the read of them COMPLETED. The
    # helper used to write FFFFFFFF and return silently when the mini-VDD's
    # diagnostic verb refused, so a capture with four registers looked like one
    # with nine and there was no field to test. Every run that reached the ring
    # must now carry a complete set.
    if ($schema -ne '1') {
        $needPost = $result -ceq 'PASS' -or $result -ceq 'EXECUTE-REFUSED' -or
                    $result -ceq 'STAGE-REFUSED'
        $groups = @('PreErr')
        # PreErr is global in both phases - it is read once, before anything is
        # submitted. PostErr is per scene in phase 6 and checked there.
        if ($needPost -and -not $isScene) { $groups += 'PostErr' }
        foreach ($group in $groups) {
            if (-not $values.ContainsKey("${group}Ok")) {
                throw ("An armed schema-2 INTEL3D0.TXT must carry ${group}Ok. " +
                       'A diagnostic that degrades quietly is worse than one ' +
                       'that is absent, because absence is visible.')
            }
            if ($values["${group}Ok"] -cne '1') {
                $at = Get-V9x3dHex32 -Values $values -Key "${group}FailIndex"
                throw ("$group is incomplete: the mini-VDD diagnostic verb " +
                       "refused at index $at. The error registers are the " +
                       'only thing that separates a rejected primitive from ' +
                       'an accepted one, so an incomplete set is a failed ' +
                       'capture rather than a partial one.')
            }
            if ((Get-V9x3dHex32 -Values $values -Key "${group}Count") -ne 9) {
                throw ("$group reports a register count other than nine.")
            }
            for ($index = 0; $index -lt 9; ++$index) {
                $key = '{0}{1:X4}' -f $group, $index
                if (-not $values.ContainsKey($key)) {
                    throw ("$group claims a complete read but $key is absent.")
                }
                # Parsed, not merely present. Testing for the key alone accepts
                # PostErr0004=garbage, which is a register value nobody can
                # read reported as a complete diagnostic.
                $null = Get-V9x3dHex32 -Values $values -Key $key
            }
        }
    }

    # The fourteen probes are now the WHOLE of the draw evidence, so a missing
    # one cannot pass. Previously the reference comparison skipped absent keys,
    # which meant a capture with no probes at all reported no mismatches.
    # The count is fixed by the driver's probe table, so it is asserted
    # against that constant and never taken from the capture. Looping to a
    # count the file supplies lets a capture declare PixelProbes=1, carry one
    # probe, and satisfy its own claim - which is not evidence of anything.
    if (-not $isScene) {
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
            # No longer "never run against hardware": it has, twice, and the
            # cause is known. The software rasteriser truncates 8-bit channels
            # to 565 and this chip rounds, so an interior probe differing by
            # one level per channel is the expected disagreement rather than
            # an open question. The Intel comparison below is the one that
            # fails; this one stays a note because the software rasteriser is
            # not wrong, only different.
            $notes += ("$mismatches of $compared pixel probes disagree with the " +
                       'software reference. REPORTED, not failed: the software ' +
                       'rasteriser truncates where this chip is measured to ' +
                       'round, so up to one level per channel is expected. ' +
                       'The measured-conversion check is what fails on a real ' +
                       'change.')
        } else {
            $notes += ("All $compared pixel probes agree with the software " +
                       'reference.')
        }
    }

    # The Intel reference: the same probes under the conversion this chip's
    # colour backend was MEASURED to use (2026-09-15, two triangle colours
    # covering all three channels). Unlike the software comparison above this
    # one FAILS, because it is no longer a prediction.
    #
    # Two references, deliberately. The software rasteriser truncates and this
    # chip rounds; that difference is understood, documented and expected, so
    # reporting it every run while failing on a real change is the only way
    # either signal stays readable. Folding them into one would mean either
    # failing on a known difference or reporting a regression as a warning.
    #
    # Presence is REQUIRED from schema 2 onward, and optional only for schema
    # 1 - the schema written before the conversion was measured, where the
    # field genuinely did not exist.
    #
    # Testing presence alone was a defect of exactly the kind this file keeps
    # collecting: a damaged or truncated schema-2 reference would make the
    # strict check vanish, and colour regressions would quietly fall back to
    # the software-reference warning. The absent check would look like a
    # passing one. A missing field in a schema that declares it is a broken
    # artefact, not an older artefact.
    $generatedSchema = 1
    if ($generated.ContainsKey('SchemaVersion')) {
        $generatedSchema = [int]$generated.SchemaVersion
    }
    if ($generatedSchema -ge 2 -and
        -not $generated.ContainsKey('IntelReferencePixels')) {
        throw ("The generated reference declares SchemaVersion " +
               "$generatedSchema but carries no IntelReferencePixels. That " +
               'field is required from schema 2 onward; without it the ' +
               'measured-conversion check would silently not run. Regenerate ' +
               'with gen-intel-3d-stream.ps1.')
    }
    if ($generated.ContainsKey('IntelReferencePixels')) {
        if ($generated.IntelReferencePixels.Count -ne $script:V9xExpectedProbes) {
            throw ('The generated Intel reference carries ' +
                   "$($generated.IntelReferencePixels.Count) probes where the " +
                   "driver publishes $($script:V9xExpectedProbes). A partial " +
                   'reference would silently excuse a partial capture.')
        }
        $bad = @()
        for ($index = 0; $index -lt $script:V9xExpectedProbes; ++$index) {
            $key = 'PX{0:X4}' -f $index
            $actual = Get-V9x3dHex32 -Values $values -Key $key
            $expected = [Convert]::ToUInt32(
                $generated.IntelReferencePixels[$index], 16)
            # Both halves of the dword, not just the low one. Inside a flat
            # region they must agree, and checking one half would accept a
            # capture where they did not.
            if ((($actual -band 0xffff) -ne $expected) -or
                ((($actual -shr 16) -band 0xffff) -ne $expected)) {
                $bad += ($key + ' reads ' + ('{0:X8}' -f $actual) +
                         ', measured rule says ' + ('{0:X4}' -f $expected))
            }
        }
        if ($bad.Count -ne 0) {
            throw ("$($bad.Count) probe(s) disagree with the MEASURED Intel " +
                   'conversion: ' + ($bad -join '; ') + '. This is not the ' +
                   'known software-rasteriser difference - it is a change in ' +
                   'what the hardware stores, or in the stream sent to it.')
        }
        $notes += ("All $($script:V9xExpectedProbes) pixel probes match the " +
                   'measured Intel conversion.')
    }
    } # end of the phase-5 global probe series

    # ------------------------------------------------------------------
    # Schema 3: the Phase 6 scene sections.
    #
    # Driven by the GENERATED scene table, not by what the capture happens to
    # contain. That direction is the whole point and it is the fourth time
    # this file has had to learn it: a loop over present keys reports on what
    # is there and calls a capture missing half its evidence complete. Every
    # scene the build defines must have a section, and every probe that scene
    # declares must have a reading.
    # ------------------------------------------------------------------
    if ($schema -eq '3') {
        if (-not $generated.ContainsKey('Scenes')) {
            throw ('A schema-3 INTEL3D0.TXT needs a generated scene table to ' +
                   'be checked against. Run gen-intel-3d-stream.ps1.')
        }
        $sceneCount = [int]$generated.SceneCount
        $declared = Get-V9x3dHex32 -Values $values -Key 'Scenes'
        if ($declared -ne $sceneCount) {
            throw ("INTEL3D0.TXT reports $declared scenes but the build " +
                   "defines $sceneCount. The capture and the arm tables " +
                   'describe different boots.')
        }
        if ((Get-V9x3dHex32 -Values $values -Key 'SceneCombinedCrc') -ne
                [Convert]::ToUInt32($generated.SceneCombinedCrc, 16)) {
            throw ('INTEL3D0.TXT carries a combined scene CRC that is not the ' +
                   'generated one. What ran is not what was reviewed.')
        }

        # How far the run got. A capture that stopped early is valid evidence
        # and must not be failed for the scenes it never reached - but it must
        # SAY where it stopped, and every scene up to there must be complete.
        $reached = $sceneCount
        if ($values.ContainsKey('SceneFailed')) {
            $reached = (Get-V9x3dHex32 -Values $values -Key 'SceneFailed') + 1
        } elseif (-not $values.ContainsKey('ScenesCompleted')) {
            throw ('A schema-3 INTEL3D0.TXT must carry either ScenesCompleted ' +
                   'or SceneFailed. Without one of them a truncated capture ' +
                   'and a complete one look identical.')
        }

        $sceneBad = @()
        $sceneMeasured = @()
        $sceneChecked = 0
        # The guards as they were BEFORE anything was submitted. Required,
        # not optional: treating their absence as "nothing to compare against"
        # made the per-scene guard check silently pass for any capture that
        # omitted them - a check that cannot fail, which is the defect this
        # file has now collected five times.
        foreach ($initial in @('GLow0', 'GUpp0')) {
            if (-not $values.ContainsKey($initial)) {
                throw ("A schema-3 INTEL3D0.TXT must carry $initial, the " +
                       'guard value read before any scene ran. Without it ' +
                       'every per-scene guard has nothing to be compared ' +
                       'against and the check passes vacuously.')
            }
        }
        $sceneGuardLow = Get-V9x3dHex32 -Values $values -Key 'GLow0'
        $sceneGuardUpp = Get-V9x3dHex32 -Values $values -Key 'GUpp0'
        for ($scene = 0; $scene -lt $reached; ++$scene) {
            $entry = @($generated.Scenes)[$scene]
            $prefix = "S$scene"
            foreach ($field in @('Id', 'Dwords', 'Crc', 'GenCrc', 'Probes')) {
                if (-not $values.ContainsKey($prefix + $field)) {
                    throw ("INTEL3D0.TXT scene $scene is missing " +
                           "$prefix$field. A scene that was reached and did " +
                           'not publish its figures is not a scene that ran.')
                }
            }
            if ((Get-V9x3dHex32 -Values $values -Key ($prefix + 'Id')) -ne
                    [int]$entry.Id) {
                throw ("INTEL3D0.TXT scene $scene reports id " +
                       "$($values[$prefix + 'Id']) where the build assigns " +
                       "$($entry.Id). Ids are how probe sets are attributed.")
            }
            if ((Get-V9x3dHex32 -Values $values -Key ($prefix + 'Dwords')) -ne
                    [int]$entry.Dwords) {
                throw ("INTEL3D0.TXT scene $scene submitted " +
                       "$($values[$prefix + 'Dwords']) dwords where the build " +
                       "generates $($entry.Dwords).")
            }
            # The driver's own CRC against the generated one, both published,
            # so a disagreement is visible here rather than only as a refusal
            # on the machine with nothing to say why.
            $sceneCrc = [Convert]::ToUInt32($entry.Crc, 16)
            foreach ($which in @('Crc', 'GenCrc')) {
                if ((Get-V9x3dHex32 -Values $values -Key ($prefix + $which)) -ne
                        $sceneCrc) {
                    throw ("INTEL3D0.TXT scene $scene has $prefix$which " +
                           "disagreeing with the generated $($entry.Crc).")
                }
            }

            # Per-scene guards, against the values read BEFORE anything was
            # submitted. Injecting a corrupted S0GLow passed before this,
            # which made the guards decorative in exactly the phase that
            # multiplied the number of draws that could damage them.
            foreach ($guard in @(
                    @{ Key = 'GLow'; Initial = $sceneGuardLow; What = 'lower' },
                    @{ Key = 'GUpp'; Initial = $sceneGuardUpp; What = 'upper' })) {
                $name = $prefix + $guard.Key
                if (-not $values.ContainsKey($name)) {
                    throw ("INTEL3D0.TXT scene $scene is missing $name. The " +
                           'guards are how a scene that wrote outside its ' +
                           'target is caught, and a scene without them cannot ' +
                           'be cleared of having done so.')
                }
                $seen = Get-V9x3dHex32 -Values $values -Key $name
                if ($seen -ne $guard.Initial) {
                    throw ("INTEL3D0.TXT scene $scene changed the " +
                           "$($guard.What) in-reserve guard from " +
                           ('{0:X8}' -f $guard.Initial) + ' to ' +
                           ('{0:X8}' -f $seen) + '. Something wrote outside ' +
                           'the render target.')
                }
            }

            # And this scene's error registers, complete. The global PostErr
            # check does not apply to a phase-6 capture, so without this the
            # diagnostic that exists to say whether the parser rejected a
            # primitive was required of nobody.
            if ($needPost) {
                if ($values[($prefix + 'PostErrOk')] -cne '1') {
                    throw ("INTEL3D0.TXT scene $scene reports " +
                           "${prefix}PostErrOk=$($values[$prefix + 'PostErrOk']). " +
                           'An incomplete register read is worse than an ' +
                           'absent one, because absence is visible.')
                }
                if ((Get-V9x3dHex32 -Values $values -Key ($prefix + 'PostErrCount')) -ne 9) {
                    throw ("INTEL3D0.TXT scene $scene claims a register count " +
                           'other than nine.')
                }
                for ($reg = 0; $reg -lt 9; ++$reg) {
                    $regKey = '{0}PostErr{1:X4}' -f $prefix, $reg
                    if (-not $values.ContainsKey($regKey)) {
                        throw ("INTEL3D0.TXT scene $scene is missing $regKey " +
                               'from a set claiming nine registers.')
                    }
                    $null = Get-V9x3dHex32 -Values $values -Key $regKey
                }
            }

            $probes = @($entry.Probes)
            if ((Get-V9x3dHex32 -Values $values -Key ($prefix + 'Probes')) -ne
                    $probes.Count) {
                throw ("INTEL3D0.TXT scene $scene reports " +
                       "$($values[$prefix + 'Probes']) probes where the build " +
                       "declares $($probes.Count). A probe count the capture " +
                       'chose for itself excuses a partial set.')
            }
            for ($probe = 0; $probe -lt $probes.Count; ++$probe) {
                $key = '{0}PX{1:X4}' -f $prefix, $probe
                if (-not $values.ContainsKey($key)) {
                    throw ("INTEL3D0.TXT scene $scene is missing $key. Every " +
                           'probe the scene declares must have a reading, or ' +
                           'the set reports on whichever ones happened to be ' +
                           'written.')
                }
                $actual = Get-V9x3dHex32 -Values $values -Key $key
                # The name key carries what the probe EXPECTED. Required, for
                # the same reason the generator requires probe names: a
                # reading whose expectation is absent cannot be judged.
                $named = $prefix + $probes[$probe].Name
                if (-not $values.ContainsKey($named)) {
                    throw ("INTEL3D0.TXT scene $scene is missing $named, the " +
                           "expectation for probe $probe. The same value is a " +
                           'result in one scene and a regression in another.')
                }

                # And the reading is COMPARED, not merely parsed.
                #
                # Every scene pixel could be DEADBEEF and this passed, which
                # made the whole per-scene probe structure decorative: it
                # required the evidence to be present and then never looked at
                # it.
                #
                # Established expectations FAIL. The fill and the triangle
                # colours are measured values - the 565 conversion is on record
                # and the fill is a constant the build owns - so a probe that
                # was supposed to read one of them and did not is a regression,
                # not a discovery.
                #
                # MEASURE probes are REPORTED. Those are the shared-edge
                # samples, and what they hold is the thing being measured; an
                # expectation there would be a guess written down as evidence.
                $low = $actual -band 0xffff
                $high = ($actual -shr 16) -band 0xffff
                if ($probes[$probe].Expect -eq 65535) {
                    $sceneMeasured += ("$key=" + ('{0:X8}' -f $actual))
                    continue
                }
                $want = $null
                if ($probes[$probe].Expect -eq 0) {
                    $want = [Convert]::ToUInt32($generated.Referencefill, 16) -band 0xffff
                } else {
                    # Triangle 0 or 1 of THIS scene, as this chip is measured
                    # to store it. Per scene, because the edge scenes draw
                    # different colours from each other and from scene 0 - a
                    # single expected colour would be right for one scene and
                    # silently wrong for the rest.
                    $tri = $probes[$probe].Expect - 1
                    $colors = @($entry.Colors)
                    if ($tri -ge $colors.Count) {
                        throw ("INTEL3D0.TXT scene $scene probe $probe " +
                               "expects triangle $tri, which the scene does " +
                               'not have.')
                    }
                    $want = [Convert]::ToUInt32($colors[$tri], 16) -band 0xffff
                }
                if ($null -ne $want) {
                    if ($low -ne $want -or $high -ne $want) {
                        $sceneBad += ("$key reads " + ('{0:X8}' -f $actual) +
                                      ' where scene ' + $scene + ' expected ' +
                                      ('{0:X4}' -f $want) + ' (' +
                                      $probes[$probe].Name + ')')
                    } else {
                        ++$sceneChecked
                    }
                }
            }
        }
        if ($sceneBad.Count -ne 0) {
            throw ("$($sceneBad.Count) scene probe(s) disagree with an " +
                   'ESTABLISHED expectation: ' + ($sceneBad -join '; ') +
                   '. The fill is a constant this build owns and the triangle ' +
                   'colours are measured, so these are regressions rather ' +
                   'than results.')
        }
        if ($sceneChecked -lt 1) {
            throw ('No scene probe was compared against an established ' +
                   'expectation. A capture whose every probe is exploratory ' +
                   'proves nothing and must not read as a pass.')
        }
        $notes += ("$sceneChecked scene probes matched their established " +
                   "expectation; $($sceneMeasured.Count) are measurements: " +
                   ($sceneMeasured -join ' '))

        # The aperture-read budget. Published before the run, so a capture
        # that reached the end must have spent about what it predicted.
        foreach ($field in @('ExpectedApertureReads', 'DriverApertureReads',
                             'MiniApertureReads')) {
            if (-not $values.ContainsKey($field)) {
                throw ("A schema-3 INTEL3D0.TXT must carry $field. The read " +
                       'budget is the measurement this boot exists to take ' +
                       'as much as the pixels are.')
            }
            $null = Get-V9x3dHex32 -Values $values -Key $field
        }
        if (-not $values.ContainsKey('SceneFailed')) {
            $expected = Get-V9x3dHex32 -Values $values -Key 'ProbeApertureReads'
            $driver = Get-V9x3dHex32 -Values $values -Key 'DriverApertureReads'
            # Driver-side reads are the probes plus the guards and heap
            # probes. Bounded rather than pinned, because the guard count
            # depends on how many scenes ran, but a driver count BELOW the
            # probe count means probes did not happen.
            if ($driver -lt $expected) {
                throw ("INTEL3D0.TXT counted $driver driver aperture reads " +
                       "for $expected declared probes. Fewer reads than " +
                       'probes means the set is incomplete however many keys ' +
                       'are present.')
            }
        }
        $notes += ("Schema 3: $reached of $sceneCount scenes checked against " +
                   'the generated table.')
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
    # Every fixture here now disagrees with the SOFTWARE reference by
    # construction, because the clean armed fixture carries what the hardware
    # was measured to store and the software rasteriser truncates. That
    # warning is correct, expected on every run, and says nothing about the
    # thing under test, so the self-test would print twenty-eight of them and
    # teach its reader to scroll past exactly the output the validator exists
    # to produce.
    #
    # Suppressed here and nowhere else: a real capture checked through this
    # script still shows them. The self-test asserts on accept-or-reject, not
    # on warning text, so nothing it tests is hidden by this.
    $WarningPreference = 'SilentlyContinue'
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
    # Schema 2 with a complete diagnostic set, which is what the armed
    # assertions now require.
    for ($i = 0; $i -lt $armedLines.Count; ++$i) {
        if ($armedLines[$i] -ceq 'SchemaVersion=1') {
            $armedLines[$i] = 'SchemaVersion=2'
        }
    }
    foreach ($group in @('PreErr', 'PostErr')) {
        for ($i = 0; $i -lt 9; ++$i) {
            $armedLines.Add(('{0}{1:X4}=00000000' -f $group, $i))
        }
        $armedLines.Add("${group}Ok=1")
        $armedLines.Add("${group}Count=00000009")
        $armedLines.Add("${group}FailIndex=FFFFFFFF")
    }
    $armedLines.Add('Access=armed-one-shot')
    $armedLines.Add('Phase4Passed=00000001')
    $armedLines.Add('HashOmitted=bulk-aperture-read-hang')
    $armedLines.Add('RowCrcOmitted=bulk-aperture-read-hang')
    $armedLines.Add('PixelProbes=0000000E')
    # Built from the MEASURED Intel reference rather than a flat value or the
    # software one, so the clean armed fixture is what the hardware actually
    # produced on 2026-09-15 and the self-test exercises the agreement path.
    #
    # It was the software reference until that measurement. That is now the
    # wrong fixture: it would fail the measured-conversion check, and a fixture
    # that has to be excused is not a fixture.
    #
    # The capture reads dwords - two 16-bit pixels - so both halves carry the
    # value, which is also what the validator checks.
    for ($probe = 0; $probe -lt 14; ++$probe) {
        $expected = if ($generated.ContainsKey('IntelReferencePixels')) {
            $generated.IntelReferencePixels[$probe]
        } elseif ($generated.ContainsKey('ReferencePixels')) {
            $generated.ReferencePixels[$probe]
        } else { '00000842' }
        $half = [Convert]::ToUInt32($expected, 16) -band 0xffff
        $armedLines.Add(('PX{0:X4}={1:X8}' -f $probe, (($half -shl 16) -bor $half)))
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
           Why = 'armed run without Phase 4 passing this boot' },
        # The gap reported against 1862916: the helper degraded silently, so a
        # capture with four registers looked like one with nine.
        @{ Old = 'PostErrOk=1'; New = 'PostErrOk=0'
           Why = 'an incomplete post-draw diagnostic read' },
        @{ Old = 'PostErrCount=00000009'; New = 'PostErrCount=00000004'
           Why = 'a diagnostic register count other than nine' },
        @{ Old = 'PostErr0007=00000000'; New = 'PostErrZZZZ=00000000'
           Why = 'a diagnostic register missing from a set claiming nine' },
        @{ Old = 'PostErr0004=00000000'; New = 'PostErr0004=garbage'
           Why = 'a diagnostic register value that is not eight hex digits' },
        @{ Old = 'PreErr0000=00000000'; New = 'PreErr0000=00000'
           Why = 'a truncated diagnostic register value' },
        @{ Old = 'PreErrOk=1'; New = 'PreErrNote=1'
           Why = 'no completeness status on the pre-draw read' },
        # The measured-conversion check, proved able to fail. The substituted
        # value is not arbitrary: 143F is exactly what the SOFTWARE rasteriser
        # produces for this triangle, so this is the one wrong value most
        # likely to be mistaken for correct - and the one a regression to
        # truncation in the stream would actually produce.
        @{ Old = 'PX0000=1C3E1C3E'; New = 'PX0000=143F143F'
           Why = 'an interior probe carrying the truncated colour' },
        # One half right and one half wrong, which a low-half-only comparison
        # would have accepted. That is how the original software-reference
        # comparison was written.
        @{ Old = 'PX0001=1C3E1C3E'; New = 'PX0001=143F1C3E'
           Why = 'an interior probe whose two halves disagree' },
        @{ Old = 'PX0007=08420842'; New = 'PX0007=1C3E1C3E'
           Why = 'an outside probe carrying the triangle colour' }
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

    # The generated reference itself, damaged rather than the capture. Nothing
    # in the capture can exercise this: a schema-2 reference with the field
    # removed is what would silently disable the strict colour check, so the
    # only way to prove the guard fires is to produce one.
    #
    # Both directions are tested. Removing the field from a schema-2 reference
    # must be refused; declaring schema 1 alongside the same removal must be
    # ACCEPTED, because that is a genuinely older artefact and failing it would
    # make the compatibility allowance a fiction.
    $referenceCases = @(
        @{ Schema = 2; Accept = $false
           Why = 'a schema-2 reference with no IntelReferencePixels' },
        @{ Schema = 1; Accept = $true
           Why = 'a schema-1 reference, which predates the measurement' }
    )
    $realGeneratedPath = $generatedPath
    foreach ($case in $referenceCases) {
        $damaged = @(Get-Content -LiteralPath $realGeneratedPath)
        $out = New-Object 'System.Collections.Generic.List[string]'
        $skipping = $false
        foreach ($line in $damaged) {
            if ($line -match '^\s*IntelReferencePixels\s*=\s*@\(') {
                $skipping = $true
                continue
            }
            if ($skipping) {
                if ($line -match '^\s*\)\s*$') { $skipping = $false }
                continue
            }
            if ($line -match '^\s*SchemaVersion\s*=') {
                $out.Add("    SchemaVersion = $($case.Schema)")
                continue
            }
            $out.Add($line)
        }
        if (($out -join "`n") -ceq ($damaged -join "`n")) {
            throw ('Self-test could not remove IntelReferencePixels from the ' +
                   'generated reference; the guard it proves is untested.')
        }
        $temp = Join-Path ([IO.Path]::GetTempPath()) (
            'v9x-gen-{0}.psd1' -f [Guid]::NewGuid())
        Set-Content -LiteralPath $temp -Value $out -Encoding Ascii
        $generatedPath = $temp
        $rejected = $false
        try { $null = Test-V9xIntel3dCapture -Lines $armedLines }
        catch { $rejected = $true }
        $generatedPath = $realGeneratedPath
        Remove-Item -LiteralPath $temp -Force
        if ($case.Accept -and $rejected) {
            throw ("The Intel 3D capture validator refused $($case.Why).")
        }
        if (-not $case.Accept -and -not $rejected) {
            throw ("The Intel 3D capture validator accepted $($case.Why); a " +
                   'damaged reference would silently disable the ' +
                   'measured-conversion check.')
        }
    }

    # ------------------------------------------------------------------
    # A schema-3 capture, built from the generated scene table.
    #
    # Built rather than pasted, so it stays correct when the scene table
    # changes - and because a hand-written fixture would encode what the
    # author believed the driver emits rather than what it does.
    # ------------------------------------------------------------------
    if ($generated.ContainsKey('Scenes')) {
        $s3 = New-Object 'System.Collections.Generic.List[string]'
        # DROPS the phase-5 globals rather than inheriting them.
        #
        # The previous fixture was the armed phase-5 one with scene sections
        # appended, so it carried HashOmitted, RowCrcOmitted, the global
        # PostErr set and the PX series - none of which a phase-6 capture
        # emits. It therefore tested what had been added and nothing about
        # whether the inherited fields were still being required, and every
        # real phase-6 capture would have been rejected.
        foreach ($line in $armedLines) {
            if ($line -like 'SchemaVersion=*') { $s3.Add('SchemaVersion=3'); continue }
            if ($line -like 'HashOmitted=*') { continue }
            if ($line -like 'RowCrcOmitted=*') { continue }
            if ($line -like 'RowCrcRowsOmitted=*') { continue }
            if ($line -like 'PostErr*') { continue }
            if ($line -like 'PX*') { continue }
            if ($line -like 'PixelProbes=*') { continue }
            if ($line -like 'PixelNext=*') { continue }
            if ($line -like 'ExpectedInside=*') { continue }
            if ($line -like 'ExpectedOutside=*') { continue }
            if ($line -like 'Centroid=*' -or $line -like 'NearV*' -or
                $line -like 'Mid*' -or $line -like 'Corner*' -or
                $line -like 'Outside*') { continue }
            $s3.Add($line)
        }
        $s3.Add('ArmPhase=00000006')
        # The pre-run guards. The phase-5 fixture never carried them, which is
        # why the per-scene guard comparison had nothing to compare against.
        $s3.Add('GLow0=A5A5A5A5')
        $s3.Add('GUpp0=00000000')
        $s3.Add('Scenes={0:X8}' -f [int]$generated.SceneCount)
        $s3.Add('ScenesAuthorised={0:X8}' -f [int]$generated.SceneAuthorisedDraws)
        $s3.Add('SceneCombinedCrc=' + $generated.SceneCombinedCrc)
        $s3.Add('ScenesCompleted={0:X8}' -f [int]$generated.SceneCount)
        $s3.Add('ReadBudgetScope=gmadr-whole-boot-incl-phase4')
        $s3.Add('ProbeApertureReads={0:X8}' -f [int]$generated.SceneTotalProbes)
        $s3.Add('DriverApertureReads={0:X8}' -f
                ([int]$generated.SceneTotalProbes + 14))
        $s3.Add('MiniApertureReads=000002A6')
        $s3.Add('Phase4ApertureReads=00000421')
        $s3.Add('ExpectedApertureReads=000008E1')
        $sceneIndex = 0
        foreach ($entry in @($generated.Scenes)) {
            $prefix = "S$sceneIndex"
            $s3.Add(('{0}Id={1:X8}' -f $prefix, [int]$entry.Id))
            $s3.Add(('{0}Dwords={1:X8}' -f $prefix, [int]$entry.Dwords))
            $s3.Add(('{0}Crc={1}' -f $prefix, $entry.Crc))
            $s3.Add(('{0}GenCrc={1}' -f $prefix, $entry.Crc))
            $s3.Add(('{0}Probes={1:X8}' -f $prefix, @($entry.Probes).Count))
            # The guards as the pre-run read found them, which is what the
            # validator compares each scene against.
            $s3.Add(('{0}GLow=A5A5A5A5' -f $prefix))
            $s3.Add(('{0}GUpp=00000000' -f $prefix))
            $s3.Add(('{0}PostErrOk=1' -f $prefix))
            $s3.Add(('{0}PostErrCount=00000009' -f $prefix))
            $s3.Add(('{0}PostErrFailIndex=FFFFFFFF' -f $prefix))
            for ($reg = 0; $reg -lt 9; ++$reg) {
                $s3.Add(('{0}PostErr{1:X4}=00000000' -f $prefix, $reg))
            }
            $probeIndex = 0
            foreach ($probe in @($entry.Probes)) {
                # Each probe reads what its scene expects, so the clean
                # fixture exercises the comparison rather than skirting it.
                $value = '00000000'
                if ($probe.Expect -eq 65535) {
                    $value = '1C3E1C3E'
                } elseif ($probe.Expect -eq 0) {
                    $half = $generated.Referencefill.Substring(4)
                    $value = $half + $half
                } else {
                    $half = @($entry.Colors)[$probe.Expect - 1]
                    $value = $half + $half
                }
                $s3.Add(('{0}{1}=x' -f $prefix, $probe.Name))
                $s3.Add(('{0}PX{1:X4}={2}' -f $prefix, $probeIndex, $value))
                ++$probeIndex
            }
            ++$sceneIndex
        }
        # The fixture must actually LACK the phase-5 globals, or dropping
        # them was a filter that matched nothing and this proves nothing.
        foreach ($forbidden in @('HashOmitted', 'RowCrcOmitted', 'PixelProbes',
                                 'PostErrOk', 'PX0000')) {
            if (@($s3 | Where-Object { $_ -like ($forbidden + '=*') }).Count -ne 0) {
                throw ("The schema-3 fixture still carries $forbidden. It is " +
                       'meant to be what a phase-6 capture actually looks ' +
                       'like, and inheriting phase-5 fields is exactly how ' +
                       'the requirement on them went unnoticed.')
            }
        }
        $null = Test-V9xIntel3dCapture -Lines $s3

        # Each of these removes evidence rather than corrupting it, because
        # reporting on what is present instead of failing on what is absent is
        # the defect this file has now had four times.
        $s3Mutations = @(
            @{ Drop = 'S2Crc='; Why = 'a scene missing its CRC' },
            @{ Drop = 'S3Probes='; Why = 'a scene missing its probe count' },
            @{ Drop = 'S4PX0007='; Why = 'the last probe of the last scene' },
            @{ Drop = 'S0Centroid='; Why = 'a probe missing its expectation' },
            @{ Drop = 'ScenesCompleted='
               Why = 'a capture that says neither how far it got nor that it stopped' },
            @{ Drop = 'ExpectedApertureReads='
               Why = 'a capture with no read budget' },
            @{ Drop = 'S1Id='; Why = 'a scene missing its id' }
        )
        foreach ($mutation in $s3Mutations) {
            $broken = @($s3 | Where-Object { $_ -notlike ($mutation.Drop + '*') })
            if ($broken.Count -eq $s3.Count) {
                throw ("Self-test mutation '$($mutation.Why)' removed nothing; " +
                       "it names a key the schema-3 fixture does not carry.")
            }
            $rejected = $false
            try { $null = Test-V9xIntel3dCapture -Lines $broken }
            catch { $rejected = $true }
            if (-not $rejected) {
                throw ('The Intel 3D capture validator accepted a schema-3 ' +
                       "capture with $($mutation.Why) removed.")
            }
        }

        # And two that corrupt rather than remove, so the check is not merely
        # a presence test.
        $s3Corruptions = @(
            @{ From = 'Scenes={0:X8}' -f [int]$generated.SceneCount
               To = 'Scenes=00000002'
               Why = 'a scene count that is not the build''s' },
            @{ From = 'S0Crc=' + @($generated.Scenes)[0].Crc
               To = 'S0Crc=DEADBEEF'
               Why = 'a scene CRC that is not the generated one' },
            # The three the reviewer found passing: guards, error completeness
            # and the pixels themselves. Each was present-and-parsed and never
            # compared, which is the same as not collecting it.
            @{ From = 'S0GLow=A5A5A5A5'; To = 'S0GLow=DEADBEEF'
               Why = 'a scene that changed the lower in-reserve guard' }
            @{ From = 'S0GUpp=00000000'; To = 'S0GUpp=DEADBEEF'
               Why = 'a scene that changed the upper in-reserve guard' }
            @{ From = 'S0PostErrOk=1'; To = 'S0PostErrOk=0'
               Why = 'a scene whose error-register read did not complete' }
            @{ From = 'S0PostErrCount=00000009'; To = 'S0PostErrCount=00000004'
               Why = 'a scene claiming fewer than nine registers' }
            @{ From = 'S0PX0000=' + ($generated.Scenes[0].Colors[0] * 2)
               To = 'S0PX0000=DEADBEEF'
               Why = 'a scene pixel that is not what the scene expected' }
        )
        foreach ($mutation in $s3Corruptions) {
            $broken = @($s3 | ForEach-Object {
                if ($_ -ceq $mutation.From) { $mutation.To } else { $_ } })
            if (($broken -join "`n") -ceq ($s3 -join "`n")) {
                throw ("Self-test corruption '$($mutation.Why)' changed " +
                       'nothing.')
            }
            $rejected = $false
            try { $null = Test-V9xIntel3dCapture -Lines $broken }
            catch { $rejected = $true }
            if (-not $rejected) {
                throw ('The Intel 3D capture validator accepted ' +
                       "$($mutation.Why).")
            }
        }
        Write-Output ('Intel 3D schema-3 self-test passed (' +
                      "$($generated.SceneCount) scenes built from the " +
                      "generated table, $($s3Mutations.Count) removals and " +
                      "$($s3Corruptions.Count) corruptions rejected).")
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
