# Validate the no-write Intel Gen3 Phase 4 command-plan artefact.
[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [string]$Path,
    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'ComputeArm')]
    [switch]$Json,
    [Parameter(ParameterSetName = 'Capture')]
    [switch]$Armed,
    # The canonical stream is fixed by compile-time constants and a layout
    # derived from two measured values, so its CRCs can be computed here with
    # no machine involved. That is what removes the no-write capture boot from
    # every arm cycle; the driver still recomputes and refuses on a mismatch.
    [Parameter(Mandatory = $true, ParameterSetName = 'ComputeArm')]
    [switch]$ComputeArm,
    [Parameter(ParameterSetName = 'ComputeArm')]
    [uint32]$VbeBytes = 0x007b0000,
    [Parameter(ParameterSetName = 'ComputeArm')]
    [uint32]$Bsm = 0x7f800000,
    [Parameter(Mandatory = $true, ParameterSetName = 'SelfTest')]
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

function ConvertFrom-V9xIntelRingIni {
    param([string[]]$Lines)
    $values = @{}
    $inSection = $false
    foreach ($line in $Lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            $inSection = $Matches[1] -ieq 'IntelRing'
            continue
        }
        if (-not $inSection -or -not $trimmed -or
            $trimmed.StartsWith(';') -or $trimmed.StartsWith('#')) { continue }
        $separator = $trimmed.IndexOf('=')
        if ($separator -le 0) { continue }
        $key = $trimmed.Substring(0, $separator).Trim()
        if ($values.ContainsKey($key)) { throw "Intel ring plan repeats key $key." }
        $values[$key] = $trimmed.Substring($separator + 1).Trim()
    }
    if ($values.Count -eq 0) { throw 'Intel ring plan has no [IntelRing] values.' }
    return $values
}

function ConvertFrom-V9xRingHex32 {
    param([hashtable]$Values, [string]$Key)
    if (-not $Values.ContainsKey($Key) -or
        $Values[$Key] -notmatch '^[0-9A-Fa-f]{8}$') {
        throw "Intel ring plan is missing an eight-digit hexadecimal $Key."
    }
    return [Convert]::ToUInt32($Values[$Key], 16)
}

function Get-V9xCrc32Dwords {
    param([uint32[]]$Dwords)
    [uint64]$crc = 0xffffffffL
    foreach ($dword in $Dwords) {
        [uint64]$value = $dword
        for ($byteIndex = 0; $byteIndex -lt 4; ++$byteIndex) {
            $crc = $crc -bxor ($value -band 0xffL)
            for ($bit = 0; $bit -lt 8; ++$bit) {
                if (($crc -band 1L) -ne 0) {
                    $crc = (($crc -shr 1) -bxor 0xedb88320L) -band 0xffffffffL
                } else {
                    $crc = ($crc -shr 1) -band 0xffffffffL
                }
            }
            $value = $value -shr 8
        }
    }
    return [uint32](($crc -bxor 0xffffffffL) -band 0xffffffffL)
}

function Test-V9xIntelRingPlan {
    param([hashtable]$Values, [switch]$Armed)
    $modePairs = if ($Armed) {
        @(@('Access', 'armed-hardware-write'), @('TokenMover', 'ARMED'),
          @('ErrataGate', '1'), @('FlushPageRead', 'STABLE'),
          @('Result', 'PASS'))
    } else {
        @(@('Access', 'no-hardware-writes'), @('TokenMover', 'READY'),
          @('ErrataGate', '0'), @('FlushPageRead', 'STABLE'),
          @('Result', 'ERRATA-GATED'))
    }
    # Name the precondition before the generic mismatch below does, because
    # "Result must be PASS" is what made a refusal cost a boot to interpret.
    if ($Values.ContainsKey('PreconditionCode')) {
        $preconditionNames = @{
            '00000001' = 'an arm key could not be read from the arm file'
            '00000002' = 'IntelArmBuildId is not the running build'
            '00000003' = 'IntelAccelDefault is not 0'
            '00000004' = 'IntelArmCrc is not eight hexadecimal digits'
            '00000005' = 'the arm contract rejected it'
            '00000006' = 'the armed CRC does not match the one latched at load'
            '00000007' = 'the two flush-page config reads disagreed'
            '00000008' = 'the sandbox layout is not the measured one'
            '00000009' = 'BSM is not 7F800000'
            '0000000A' = 'PGTBL_CTL is not 7FFC0001'
            '0000000B' = 'a ring register was not zero at entry'
            '0000000C' = 'the GTT hash is not the Phase 2 baseline'
            '0000000D' = 'the event journal is full or dropped a record'
            '0000000E' = 'INTELMM.TXT does not say PASS'
            '0000000F' = 'INTELGTT.TXT does not say PASS'
            '00000010' = 'a mini-VDD diagnostic register read failed'
            '00000011' = 'EIR is not clear'
            '00000012' = 'ESR is not clear'
        }
        $armRejectNames = @{
            '00000001' = 'not enabled this boot'; '00000002' = 'Safe Mode'
            '00000003' = 'errata gate closed';    '00000004' = 'PCI identity'
            '00000005' = 'phase';                 '00000006' = 'token'
            '00000007' = 'command CRC'
        }
        $code = $Values.PreconditionCode
        $detail = if ($preconditionNames.ContainsKey($code)) {
            $preconditionNames[$code]
        } else { "unknown precondition code $code" }
        if ($code -ceq '00000005' -and $Values.ContainsKey('PreconditionArmReject')) {
            $reject = $Values.PreconditionArmReject
            if ($armRejectNames.ContainsKey($reject)) {
                $detail += ": $($armRejectNames[$reject])"
            }
        }
        throw "Intel Phase 4 refused before any write: $detail."
    }
    foreach ($pair in $modePairs) {
        if (-not $Values.ContainsKey($pair[0]) -or $Values[$pair[0]] -cne $pair[1]) {
            throw "Intel ring plan $($pair[0]) must be $($pair[1])."
        }
    }
    if (-not $Values.ContainsKey('CaptureBuildId') -or
        $Values.CaptureBuildId -cnotmatch '^[A-Za-z0-9._+-]{1,63}$') {
        throw 'Intel ring plan has no valid capture build ID.'
    }

    [uint32]$flushPage0 = ConvertFrom-V9xRingHex32 $Values 'FlushPageCfg0'
    [uint32]$flushPage1 = ConvertFrom-V9xRingHex32 $Values 'FlushPageCfg1'
    if ($flushPage0 -ne $flushPage1 -or $flushPage0 -eq [uint32]::MaxValue) {
        throw 'Intel host-bridge D0:F0 60h reads are unstable or invalid.'
    }

    [uint32]$heap = ConvertFrom-V9xRingHex32 $Values 'HeapBytes'
    [uint32]$reserve = ConvertFrom-V9xRingHex32 $Values 'ReserveOffset'
    [uint32]$reservePhysical = ConvertFrom-V9xRingHex32 $Values 'ReservePhysical'
    [uint32]$ring = ConvertFrom-V9xRingHex32 $Values 'RingOffset'
    [uint32]$ringPhysical = ConvertFrom-V9xRingHex32 $Values 'RingPhysical'
    [uint32]$ringBytes = ConvertFrom-V9xRingHex32 $Values 'RingBytes'
    [uint32]$hws = ConvertFrom-V9xRingHex32 $Values 'HwsOffset'
    [uint32]$hwsPhysical = ConvertFrom-V9xRingHex32 $Values 'HwsPhysical'
    [uint32]$scratch = ConvertFrom-V9xRingHex32 $Values 'ScratchOffset'
    [uint32]$scratchPhysical = ConvertFrom-V9xRingHex32 $Values 'ScratchPhysical'
    [uint32]$scratchBytes = ConvertFrom-V9xRingHex32 $Values 'ScratchBytes'

    if ($heap -ne $reserve -or $ring -ne $reserve -or $ringBytes -ne 0x10000 -or
        $hws -ne $ring + $ringBytes -or $scratch -ne $hws + 0x1000 -or
        $scratchBytes -ne 0x1000 -or $scratch + $scratchBytes -gt $reserve + 0x100000 -or
        $ringPhysical -ne $reservePhysical -or
        $hwsPhysical -ne $reservePhysical + ($hws - $reserve) -or
        $scratchPhysical -ne $reservePhysical + ($scratch - $reserve) -or
        (ConvertFrom-V9xRingHex32 $Values 'RingCtl') -ne 0x0000f001) {
        throw 'Intel ring plan layout is inconsistent with the reviewed 1-MiB sandbox.'
    }

    [uint32[]]$probe = 0..1 | ForEach-Object {
        ConvertFrom-V9xRingHex32 $Values ("PD{0}" -f $_)
    }
    [uint32[]]$blt = 0..7 | ForEach-Object {
        ConvertFrom-V9xRingHex32 $Values ("BD{0}" -f $_)
    }
    [uint32[]]$expectedProbe = 0x00000000, 0x02000000
    [uint32[]]$expectedBlt = @(
        0x54300004, 0x03f00020, 0x00000000, 0x00080008,
        [uint32]($scratch + 0x100), 0x55aa33cc, 0x02000000, 0x00000000)
    if ((Compare-Object $probe $expectedProbe -SyncWindow 0) -or
        (Compare-Object $blt $expectedBlt -SyncWindow 0)) {
        throw 'Intel ring plan contains a dword outside the exact Phase 4 command stream.'
    }
    [uint32[]]$combined = $probe + $blt
    if ((ConvertFrom-V9xRingHex32 $Values 'ProbeCrc') -ne
            (Get-V9xCrc32Dwords $probe) -or
        (ConvertFrom-V9xRingHex32 $Values 'BltCrc') -ne
            (Get-V9xCrc32Dwords $blt) -or
        (ConvertFrom-V9xRingHex32 $Values 'ArmPacketCrc') -ne
            (Get-V9xCrc32Dwords $combined)) {
        throw 'Intel ring plan command CRC does not match its dwords.'
    }
    [uint32]$wrapNoops = ConvertFrom-V9xRingHex32 $Values 'WrapNoopDwords'
    if ($wrapNoops -ne (($ringBytes - 8) / 4)) {
        throw 'Intel ring plan wrap fill does not occupy the legal free space.'
    }
    [uint32[]]$execution = $probe + ([uint32[]](1..$wrapNoops | ForEach-Object { 0 })) +
        $probe + $blt
    [uint32]$executionCrc = Get-V9xCrc32Dwords $execution
    if ((ConvertFrom-V9xRingHex32 $Values 'ArmExecutionCrc') -ne $executionCrc) {
        throw 'Intel ring plan execution CRC does not cover the full wrap stream.'
    }
    if ($Armed) {
        foreach ($pair in @(
            @('StageMirror', 'PASS'), @('PreSnapshot', 'PASS'),
            @('ScratchGuard', 'PASS'), @('PostSnapshot', 'PASS'),
            @('S10Result', 'PASS'), @('S12Result', 'PASS'),
            @('IntentStep', 'S11'))) {
            if (-not $Values.ContainsKey($pair[0]) -or
                $Values[$pair[0]] -cne $pair[1]) {
                throw "Intel armed capture $($pair[0]) must be $($pair[1])."
            }
        }
        if (-not $Values.ContainsKey('Intent') -or
            $Values.Intent -cnotmatch '^[A-Za-z0-9._-]{1,63}$' -or
            -not $Values.ContainsKey('IntentBuildId') -or
            $Values.IntentBuildId -cne $Values.CaptureBuildId -or
            (ConvertFrom-V9xRingHex32 $Values 'IntentCrc') -ne $executionCrc) {
            throw 'Intel armed capture token, build ID or intent CRC is invalid.'
        }
        $heads = @{ 5 = 0; 6 = 8; 7 = 0; 8 = 8; 9 = 40; 11 = 0 }
        foreach ($step in 5, 6, 7, 8, 9, 11) {
            $prefix = 'S{0:D2}' -f $step
            if (-not $Values.ContainsKey("${prefix}Result") -or
                $Values["${prefix}Result"] -cne 'PASS') {
                throw "Intel armed capture $prefix did not complete."
            }
            [uint32]$head = ConvertFrom-V9xRingHex32 $Values "${prefix}Head"
            [uint32]$tail = ConvertFrom-V9xRingHex32 $Values "${prefix}Tail"
            [uint32]$failure = ConvertFrom-V9xRingHex32 $Values "${prefix}Failure"
            [uint32]$elapsed = ConvertFrom-V9xRingHex32 $Values "${prefix}Ms"
            [uint32]$polls = ConvertFrom-V9xRingHex32 $Values "${prefix}Polls"
            [uint32]$ctl = ConvertFrom-V9xRingHex32 $Values "${prefix}Ctl"
            [uint32]$start = ConvertFrom-V9xRingHex32 $Values "${prefix}Start"
            [uint32]$expectedCtl = if ($step -eq 11) { 0 } else { 0x0000f001 }
            [uint32]$expectedStart = if ($step -eq 11) { 0 } else { 0x006b0000 }
            if (($head -band 0x001ffffc) -ne $heads[$step] -or
                $tail -ne $heads[$step] -or $failure -ne 0 -or
                $elapsed -gt 200 -or $polls -gt 1000000 -or
                ($ctl -band 0xfffff7ffL) -ne $expectedCtl -or
                $start -ne $expectedStart) {
                throw "Intel armed capture $prefix readback or bound failed."
            }
        }
        for ($index = 0; $index -lt 7; ++$index) {
            if ((ConvertFrom-V9xRingHex32 $Values "PreErr$index") -ne
                (ConvertFrom-V9xRingHex32 $Values "PostErr$index")) {
                throw "Intel armed capture error register $index changed."
            }
        }
        for ($index = 0; $index -lt 20; ++$index) {
            $suffix = '{0:D2}' -f $index
            [uint32]$before = ConvertFrom-V9xRingHex32 $Values "PreM$suffix"
            [uint32]$after = ConvertFrom-V9xRingHex32 $Values "PostM$suffix"
            if ($before -ne $after -or
                ($index -eq 0 -and $before -ne 0x7ffc0001) -or
                ($index -ge 1 -and $index -le 4 -and $before -ne 0)) {
                throw "Intel armed capture MMIO fingerprint $suffix changed or is unexpected."
            }
        }
    }
    return [ordered]@{
        Result = $Values.Result
        HeapBytes = $heap
        RingOffset = ('{0:X8}' -f $ring)
        HwsOffset = ('{0:X8}' -f $hws)
        ScratchOffset = ('{0:X8}' -f $scratch)
        FlushPageCfg60 = ('{0:X8}' -f $flushPage0)
        FlushPageEnabled = [bool]($flushPage0 -band 1)
        FlushPageAddress = ('{0:X8}' -f ($flushPage0 -band 0xfffff000L))
        ArmPacketCrc = ('{0:X8}' -f (Get-V9xCrc32Dwords $combined))
        ArmExecutionCrc = ('{0:X8}' -f $executionCrc)
    }
}

if ($PSCmdlet.ParameterSetName -eq 'SelfTest') {
    $sample = ConvertFrom-V9xIntelRingIni @(
        '[IntelRing]', 'Access=no-hardware-writes', 'ErrataGate=0',
        'TokenMover=READY',
        'CaptureBuildId=p4-self-test-build',
        'FlushPageCfg0=00000000', 'FlushPageCfg1=00000000',
        'FlushPageRead=STABLE',
        'HeapBytes=006B0000', 'ReserveOffset=006B0000',
        'ReservePhysical=7FEB0000', 'RingOffset=006B0000',
        'RingPhysical=7FEB0000', 'RingBytes=00010000', 'RingCtl=0000F001',
        'HwsOffset=006C0000', 'HwsPhysical=7FEC0000',
        'ScratchOffset=006C1000', 'ScratchPhysical=7FEC1000',
        'ScratchBytes=00001000', 'PD0=00000000', 'PD1=02000000',
        'BD0=54300004', 'BD1=03F00020', 'BD2=00000000', 'BD3=00080008',
        'BD4=006C1100', 'BD5=55AA33CC', 'BD6=02000000', 'BD7=00000000',
        'ProbeCrc=8B2CBE45', 'BltCrc=270BDC4C', 'ArmPacketCrc=BA0895B6',
        'WrapNoopDwords=00003FFE', 'ArmExecutionCrc=A0DA64A1',
        'Result=ERRATA-GATED')
    $result = Test-V9xIntelRingPlan $sample
    $sample.FlushPageCfg1 = '00000001'
    try {
        $null = Test-V9xIntelRingPlan $sample
        throw 'The ring-plan validator accepted mismatched 60h reads.'
    } catch {
        if ($_.Exception.Message -eq
                'The ring-plan validator accepted mismatched 60h reads.') { throw }
    }
    $armedFixture = $sample.Clone()
    $armedFixture.FlushPageCfg1 = '00000000'
    $armedFixture.Access = 'armed-hardware-write'
    $armedFixture.TokenMover = 'ARMED'
    $armedFixture.ErrataGate = '1'
    $armedFixture.Result = 'PASS'
    $armedFixture.Intent = 'p4-self-test'
    $armedFixture.IntentBuildId = 'p4-self-test-build'
    $armedFixture.IntentCrc = 'A0DA64A1'
    $armedFixture.IntentStep = 'S11'
    foreach ($key in 'StageMirror', 'PreSnapshot', 'ScratchGuard',
                   'PostSnapshot', 'S10Result', 'S12Result') {
        $armedFixture[$key] = 'PASS'
    }
    foreach ($step in 5, 6, 7, 8, 9, 11) {
        $prefix = 'S{0:D2}' -f $step
        $head = @{ 5 = 0; 6 = 8; 7 = 0; 8 = 8; 9 = 40; 11 = 0 }[$step]
        $armedFixture["${prefix}Result"] = 'PASS'
        $armedFixture["${prefix}Head"] = '{0:X8}' -f $head
        $armedFixture["${prefix}Tail"] = '{0:X8}' -f $head
        $armedFixture["${prefix}Failure"] = '00000000'
        $armedFixture["${prefix}Ms"] = '00000001'
        $armedFixture["${prefix}Polls"] = '00000001'
        $armedFixture["${prefix}Ctl"] = if ($step -eq 11) {
            '00000000'
        } else { '0000F001' }
        $armedFixture["${prefix}Start"] = if ($step -eq 11) {
            '00000000'
        } else { '006B0000' }
    }
    for ($index = 0; $index -lt 7; ++$index) {
        $armedFixture["PreErr$index"] = '00000000'
        $armedFixture["PostErr$index"] = '00000000'
    }
    for ($index = 0; $index -lt 20; ++$index) {
        $suffix = '{0:D2}' -f $index
        $value = if ($index -eq 0) { '7FFC0001' } else { '00000000' }
        $armedFixture["PreM$suffix"] = $value
        $armedFixture["PostM$suffix"] = $value
    }
    $null = Test-V9xIntelRingPlan $armedFixture -Armed
    foreach ($mutation in @(
        @{ Key = 'S07Head'; Value = '00200008' },
        @{ Key = 'ScratchGuard'; Value = 'FAIL' },
        @{ Key = 'S06Result'; Value = 'FAIL' },
        @{ Key = 'S06Ms'; Value = '000000C9' },
        @{ Key = 'S05Ctl'; Value = '00000000' },
        @{ Key = 'PostErr4'; Value = '00000001' },
        @{ Key = 'PostM08'; Value = '00000001' })) {
        $changed = $armedFixture.Clone()
        $changed[$mutation.Key] = $mutation.Value
        try {
            $null = Test-V9xIntelRingPlan $changed -Armed
            throw "Armed validator accepted $($mutation.Key) mutation."
        } catch {
            if ($_.Exception.Message -eq
                    "Armed validator accepted $($mutation.Key) mutation.") { throw }
        }
    }
    Write-Host "Intel ring-plan validator self-test passed ($($result.ArmPacketCrc))."
    return
}

if ($PSCmdlet.ParameterSetName -eq 'ComputeArm') {
    # Mirrors v9x_i9xx_sandbox_calculate and the Phase 4 stream builders.
    [uint32]$reserveBytes = 0x100000
    [uint32]$ringBytes = 0x10000
    [uint32]$pageBytes = 0x1000
    if ($VbeBytes -lt $reserveBytes -or ($VbeBytes -band ($pageBytes - 1)) -ne 0 -or
        ($Bsm -band ($pageBytes - 1)) -ne 0) {
        throw 'VbeBytes/Bsm are not a page-aligned layout the driver would accept.'
    }
    [uint32]$reserveOffset = $VbeBytes - $reserveBytes
    [uint32]$ringOffset = $reserveOffset
    [uint32]$hwsOffset = $ringOffset + $ringBytes
    [uint32]$scratchOffset = $hwsOffset + $pageBytes
    [uint32[]]$probe = 0x00000000, 0x02000000
    [uint32[]]$blt = @(
        0x54300004, 0x03f00020, 0x00000000, 0x00080008,
        [uint32]($scratchOffset + 0x100), 0x55aa33cc, 0x02000000, 0x00000000)
    [uint32]$wrapNoops = ($ringBytes - 8) / 4
    [uint32[]]$execution = $probe + ([uint32[]](1..$wrapNoops | ForEach-Object { 0 })) +
        $probe + $blt
    $arm = [ordered]@{
        VbeBytes = '{0:X8}' -f $VbeBytes
        Bsm = '{0:X8}' -f $Bsm
        ReserveOffset = '{0:X8}' -f $reserveOffset
        ReservePhysical = '{0:X8}' -f ([uint32]($Bsm + $reserveOffset))
        RingOffset = '{0:X8}' -f $ringOffset
        HwsOffset = '{0:X8}' -f $hwsOffset
        ScratchOffset = '{0:X8}' -f $scratchOffset
        WrapNoopDwords = '{0:X8}' -f $wrapNoops
        ArmPacketCrc = '{0:X8}' -f (Get-V9xCrc32Dwords ($probe + $blt))
        ArmExecutionCrc = '{0:X8}' -f (Get-V9xCrc32Dwords $execution)
    }
    if ($Json) { [pscustomobject]$arm | ConvertTo-Json -Compress }
    else { [pscustomobject]$arm | Format-List }
    return
}

$resolved = (Resolve-Path -LiteralPath $Path).Path
$result = Test-V9xIntelRingPlan (ConvertFrom-V9xIntelRingIni (
    Get-Content -LiteralPath $resolved)) -Armed:$Armed
if ($Json) { $result | ConvertTo-Json } else { $result | Format-List }
