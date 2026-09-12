# Validate the no-write Intel Gen3 Phase 4 command-plan artefact.
[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [string]$Path,
    [Parameter(ParameterSetName = 'Capture')]
    [switch]$Json,
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
    param([hashtable]$Values)
    foreach ($pair in @(
        @('Access', 'no-hardware-writes'),
        @('ErrataGate', '0'),
        @('Result', 'ERRATA-GATED'))) {
        if (-not $Values.ContainsKey($pair[0]) -or $Values[$pair[0]] -cne $pair[1]) {
            throw "Intel ring plan $($pair[0]) must be $($pair[1])."
        }
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
        $scratchBytes -ne 0x1000 -or $scratch + $scratchBytes -gt $reserve + 0x20000 -or
        $ringPhysical -ne $reservePhysical -or
        $hwsPhysical -ne $reservePhysical + ($hws - $reserve) -or
        $scratchPhysical -ne $reservePhysical + ($scratch - $reserve) -or
        (ConvertFrom-V9xRingHex32 $Values 'RingCtl') -ne 0x0000f001) {
        throw 'Intel ring plan layout is inconsistent with the reviewed 128-KiB sandbox.'
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
    return [ordered]@{
        Result = $Values.Result
        HeapBytes = $heap
        RingOffset = ('{0:X8}' -f $ring)
        HwsOffset = ('{0:X8}' -f $hws)
        ScratchOffset = ('{0:X8}' -f $scratch)
        ArmPacketCrc = ('{0:X8}' -f (Get-V9xCrc32Dwords $combined))
    }
}

if ($PSCmdlet.ParameterSetName -eq 'SelfTest') {
    $sample = ConvertFrom-V9xIntelRingIni @(
        '[IntelRing]', 'Access=no-hardware-writes', 'ErrataGate=0',
        'HeapBytes=00790000', 'ReserveOffset=00790000',
        'ReservePhysical=7FF90000', 'RingOffset=00790000',
        'RingPhysical=7FF90000', 'RingBytes=00010000', 'RingCtl=0000F001',
        'HwsOffset=007A0000', 'HwsPhysical=7FFA0000',
        'ScratchOffset=007A1000', 'ScratchPhysical=7FFA1000',
        'ScratchBytes=00001000', 'PD0=00000000', 'PD1=02000000',
        'BD0=54300004', 'BD1=03F00020', 'BD2=00000000', 'BD3=00080008',
        'BD4=007A1100', 'BD5=55AA33CC', 'BD6=02000000', 'BD7=00000000',
        'ProbeCrc=8B2CBE45', 'BltCrc=B97BAB96', 'ArmPacketCrc=2478E26C',
        'Result=ERRATA-GATED')
    $result = Test-V9xIntelRingPlan $sample
    Write-Host "Intel ring-plan validator self-test passed ($($result.ArmPacketCrc))."
    return
}

$resolved = (Resolve-Path -LiteralPath $Path).Path
$result = Test-V9xIntelRingPlan (ConvertFrom-V9xIntelRingIni (
    Get-Content -LiteralPath $resolved))
if ($Json) { $result | ConvertTo-Json } else { $result | Format-List }
