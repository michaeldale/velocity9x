# Validate the read-only Intel Gen3 Phase 1 artefact copied back from hardware.
[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [string]$Path,
    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65535)][int]$ExpectedWidth = 1024,
    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65535)][int]$ExpectedHeight = 576,
    [Parameter(ParameterSetName = 'Capture')]
    [ValidateSet(8, 16, 32)][int]$ExpectedBitsPerPixel = 16,
    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65535)][int]$ExpectedPitch = 2048,
    [Parameter(ParameterSetName = 'Capture')]
    [switch]$Json,
    [Parameter(Mandatory = $true, ParameterSetName = 'SelfTest')]
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$expectedOffsets = @(
    0x00002020, 0x00002030, 0x00002034, 0x00002038, 0x0000203c,
    0x00002080, 0x00070008, 0x00060000, 0x0006000c, 0x0006001c,
    0x00070180, 0x00070184, 0x00070188, 0x00071008, 0x00061000,
    0x0006100c, 0x0006101c, 0x00071180, 0x00071184, 0x00071188)

function ConvertFrom-V9xHex32 {
    param([hashtable]$Values, [string]$Key)
    if (-not $Values.ContainsKey($Key) -or
        $Values[$Key] -notmatch '^[0-9A-Fa-f]{8}$') {
        throw "Intel MMIO capture is missing an eight-digit hexadecimal $Key."
    }
    return [Convert]::ToUInt32($Values[$Key], 16)
}

function ConvertFrom-V9xIntelIni {
    param([string[]]$Lines)
    $values = @{}
    $inSection = $false
    foreach ($line in $Lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            $inSection = $Matches[1] -ieq 'IntelMmio'
            continue
        }
        if (-not $inSection -or -not $trimmed -or
            $trimmed.StartsWith(';') -or $trimmed.StartsWith('#')) {
            continue
        }
        $separator = $trimmed.IndexOf('=')
        if ($separator -le 0) { continue }
        $key = $trimmed.Substring(0, $separator).Trim()
        if ($values.ContainsKey($key)) {
            throw "Intel MMIO capture repeats key $key."
        }
        $values[$key] = $trimmed.Substring($separator + 1).Trim()
    }
    if ($values.Count -eq 0) {
        throw 'Intel MMIO capture has no [IntelMmio] values.'
    }
    return $values
}

function Test-V9xIntelCapture {
    param(
        [string[]]$Lines,
        [int]$Width,
        [int]$Height,
        [int]$BitsPerPixel,
        [int]$Pitch
    )
    $values = ConvertFrom-V9xIntelIni -Lines $Lines
    if ($values['Access'] -cne 'read-only') {
        throw 'Intel MMIO capture does not declare Access=read-only.'
    }
    if ($values['BarProvenance'] -cne 'PCI-BAR0-runtime') {
        throw 'Intel MMIO capture does not declare runtime PCI BAR0 provenance.'
    }
    if ($values['Result'] -cne 'PASS') {
        throw "Intel MMIO Phase 1 did not pass: Result=$($values['Result'])."
    }

    $bar0 = ConvertFrom-V9xHex32 -Values $values -Key 'Bar0'
    if ($bar0 -lt 0x01000000 -or $bar0 -gt [uint32]4294443008 -or
        ($bar0 -band 0x0007ffff) -ne 0) {
        throw ('Intel MMIO BAR0 is outside the accepted aligned 512-KiB ' +
               ('window: {0:X8}.' -f $bar0))
    }
    $flags = ConvertFrom-V9xHex32 -Values $values -Key 'Flags'
    if (($flags -band 0x0000003f) -ne 0x0000003f) {
        throw ('Intel MMIO capture lacks a Phase 1 relationship bit: ' +
               ('Flags={0:X8}.' -f $flags))
    }

    $nonzero = $false
    $notOnes = $false
    for ($index = 0; $index -lt $expectedOffsets.Count; ++$index) {
        $prefix = 'R{0:X2}' -f $index
        $offset = ConvertFrom-V9xHex32 -Values $values -Key ($prefix + 'O')
        $first = ConvertFrom-V9xHex32 -Values $values -Key ($prefix + 'A')
        $second = ConvertFrom-V9xHex32 -Values $values -Key ($prefix + 'B')
        $delta = ConvertFrom-V9xHex32 -Values $values -Key ($prefix + 'D')
        if ($offset -ne [uint32]$expectedOffsets[$index]) {
            throw ('Intel MMIO allowlist mismatch at index {0:X2}: ' -f $index) +
                  ('expected {0:X8}, got {1:X8}.' -f
                   $expectedOffsets[$index], $offset)
        }
        $actualDelta = [uint32]($first -bxor $second)
        if ($delta -ne $actualDelta -or $delta -ne 0) {
            throw ('Intel MMIO repeat read changed at index {0:X2}: ' -f $index) +
                  ('A={0:X8} B={1:X8} D={2:X8}.' -f $first, $second, $delta)
        }
        if ($first -ne 0) { $nonzero = $true }
        if ($first -ne [uint32]::MaxValue) { $notOnes = $true }
    }
    if (-not $nonzero -or -not $notOnes) {
        throw 'Intel MMIO allowlist is uniformly zero or all-ones.'
    }

    $decoded = @{
        TimingWidth = $Width; TimingHeight = $Height
        SourceWidth = $Width; SourceHeight = $Height
        PlaneBpp = $BitsPerPixel; PlaneStride = $Pitch
    }
    foreach ($key in $decoded.Keys) {
        $actual = ConvertFrom-V9xHex32 -Values $values -Key $key
        if ($actual -ne [uint32]$decoded[$key]) {
            throw "$key is $actual; expected $($decoded[$key])."
        }
    }
    $totalWidth = ConvertFrom-V9xHex32 -Values $values -Key 'TotalWidth'
    $totalHeight = ConvertFrom-V9xHex32 -Values $values -Key 'TotalHeight'
    if ($totalWidth -lt $Width -or $totalHeight -lt $Height) {
        throw 'Decoded totals are smaller than the active mode.'
    }
    $livePipe = ConvertFrom-V9xHex32 -Values $values -Key 'LivePipe'
    if ($livePipe -gt 1) { throw "LivePipe is neither pipe A nor B: $livePipe." }
    $planeAddress = ConvertFrom-V9xHex32 -Values $values -Key 'PlaneAddress'

    return [pscustomobject]@{
        Result = 'PASS'
        Bar0 = '{0:X8}' -f $bar0
        Flags = '{0:X8}' -f $flags
        RingQuiescent = (($flags -band 0x00000040) -ne 0)
        LivePipe = $livePipe
        Mode = "${Width}x${Height}x${BitsPerPixel}"
        Pitch = $Pitch
        PlaneAddress = '{0:X8}' -f $planeAddress
        Registers = $expectedOffsets.Count
    }
}

if ($SelfTest) {
    $lines = @('[IntelMmio]', 'Access=read-only',
               'BarProvenance=PCI-BAR0-runtime', 'Bar0=F0000000',
               'Flags=0000007F', 'LivePipe=00000001',
               'TimingWidth=00000400', 'TimingHeight=00000240',
               'TotalWidth=00000540', 'TotalHeight=000002A0',
               'SourceWidth=00000400', 'SourceHeight=00000240',
               'PlaneBpp=00000010', 'PlaneStride=00000800',
               'PlaneAddress=00100000', 'Result=PASS')
    for ($index = 0; $index -lt $expectedOffsets.Count; ++$index) {
        $prefix = 'R{0:X2}' -f $index
        $value = if ($index -eq 0) { 0x7f840001 } else { 0 }
        $lines += $prefix + 'O={0:X8}' -f $expectedOffsets[$index]
        $lines += $prefix + 'A={0:X8}' -f $value
        $lines += $prefix + 'B={0:X8}' -f $value
        $lines += $prefix + 'D=00000000'
    }
    $null = Test-V9xIntelCapture -Lines $lines -Width 1024 -Height 576 `
        -BitsPerPixel 16 -Pitch 2048
    foreach ($mutation in @(
        @{ Old = 'R00D=00000000'; New = 'R00D=00000001' },
        @{ Old = 'Result=PASS'; New = 'Result=REVIEW' },
        @{ Old = 'Bar0=F0000000'; New = 'Bar0=F0010000' })) {
        $broken = @($lines | ForEach-Object {
            if ($_ -ceq $mutation.Old) { $mutation.New } else { $_ }
        })
        $rejected = $false
        try {
            $null = Test-V9xIntelCapture -Lines $broken -Width 1024 -Height 576 `
                -BitsPerPixel 16 -Pitch 2048
        } catch { $rejected = $true }
        if (-not $rejected) { throw "Self-test mutation was accepted: $($mutation.New)" }
    }
    Write-Output 'Intel MMIO capture validator self-test passed (clean accepted, 3 mutations rejected).'
    exit 0
}

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Intel MMIO capture does not exist: $Path"
}
$capture = Test-V9xIntelCapture -Lines @(Get-Content -LiteralPath $Path) `
    -Width $ExpectedWidth -Height $ExpectedHeight `
    -BitsPerPixel $ExpectedBitsPerPixel -Pitch $ExpectedPitch
if ($Json) { $capture | ConvertTo-Json -Compress } else { $capture | Format-List }
