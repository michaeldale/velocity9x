# Validate the read-only Intel Gen3 Phase 2 artefacts copied from hardware.
[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [string]$Path,
    [Parameter(ParameterSetName = 'Capture')]
    [string]$BinaryPath,
    [Parameter(ParameterSetName = 'Capture')]
    [switch]$Json,
    [Parameter(Mandatory = $true, ParameterSetName = 'SelfTest')]
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$gttEntries = 65536
$gttBytes = 0x40000
$pageBytes = 4096
$reserveBytes = 0x20000
$requiredFlags = 0x7f

function ConvertFrom-V9xIntelGttIni {
    param([string[]]$Lines)
    $values = @{}
    $inSection = $false
    foreach ($line in $Lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            $inSection = $Matches[1] -ieq 'IntelGtt'
            continue
        }
        if (-not $inSection -or -not $trimmed -or
            $trimmed.StartsWith(';') -or $trimmed.StartsWith('#')) { continue }
        $separator = $trimmed.IndexOf('=')
        if ($separator -le 0) { continue }
        $key = $trimmed.Substring(0, $separator).Trim()
        if ($values.ContainsKey($key)) { throw "Intel GTT capture repeats key $key." }
        $values[$key] = $trimmed.Substring($separator + 1).Trim()
    }
    if ($values.Count -eq 0) { throw 'Intel GTT capture has no [IntelGtt] values.' }
    return $values
}

function ConvertFrom-V9xGttHex32 {
    param([hashtable]$Values, [string]$Key)
    if (-not $Values.ContainsKey($Key) -or
        $Values[$Key] -notmatch '^[0-9A-Fa-f]{8}$') {
        throw "Intel GTT capture is missing an eight-digit hexadecimal $Key."
    }
    return [Convert]::ToUInt32($Values[$Key], 16)
}

function Get-V9xFnv1a32 {
    param([byte[]]$Bytes)
    [uint64]$hash = 2166136261
    foreach ($byte in $Bytes) {
        $hash = (($hash -bxor [uint64]$byte) * 16777619) -band 0xffffffffL
    }
    return [uint32]$hash
}

function Get-V9xGttAnalysis {
    param([byte[]]$Bytes, [uint32]$Bsm, [uint32]$VbeBytes)
    [uint32]$present = 0
    [uint32]$uncached = 0
    [uint32]$local = 0
    [uint32]$cached = 0
    [uint32]$unknown = 0
    [uint32]$prefix = 0
    $prefixOpen = $true
    $runs = [System.Collections.Generic.List[object]]::new()
    $current = $null

    for ($index = 0; $index -lt $gttEntries; ++$index) {
        [uint32]$raw = [BitConverter]::ToUInt32($Bytes, $index * 4)
        [uint32]$physical = $raw -band 0xfffff000
        [uint32]$attributes = $raw -band 0x00000fff
        $isPresent = ($raw -band 1) -ne 0
        [uint32]$cacheBits = $raw -band 6
        if ($isPresent) {
            ++$present
            if ($cacheBits -eq 0) { ++$uncached }
            elseif ($cacheBits -eq 2) { ++$local }
            elseif ($cacheBits -eq 6) { ++$cached }
        }
        if ($attributes -notin @(0, 1, 3, 7)) { ++$unknown }
        [uint32]$expectedPhysical = [uint32]([uint64]$Bsm +
            [uint64]$index * $pageBytes)
        if ($prefixOpen -and $isPresent -and $physical -eq $expectedPhysical) {
            ++$prefix
        } else { $prefixOpen = $false }

        $accept = $false
        if ($null -ne $current -and $isPresent -eq $current.Present -and
            $attributes -eq $current.Attributes) {
            if (-not $isPresent) { $accept = $raw -eq $current.PreviousRaw }
            else {
                [uint32]$stride = [uint32]($physical - $current.PreviousPhysical)
                $accept = ($stride -eq 0 -or $stride -eq $pageBytes) -and
                    ($current.Count -eq 1 -or $stride -eq $current.Stride)
                if ($accept -and $current.Count -eq 1) { $current.Stride = $stride }
            }
        }
        if (-not $accept) {
            if ($null -ne $current) { $runs.Add([pscustomobject]$current) }
            $current = [ordered]@{
                Start = [uint32]$index; Count = [uint32]1
                FirstPhysical = $physical; PreviousPhysical = $physical
                PreviousRaw = $raw; Stride = [uint32]::MaxValue
                Present = $isPresent; Attributes = $attributes
            }
        } else {
            ++$current.Count
            $current.PreviousPhysical = $physical
            $current.PreviousRaw = $raw
        }
    }
    if ($null -ne $current) { $runs.Add([pscustomobject]$current) }
    return [pscustomobject]@{
        Present = $present; Uncached = $uncached; Local = $local
        Cached = $cached; Unknown = $unknown; BackedPrefix = $prefix
        Runs = $runs
    }
}

function Test-V9xIntelGttCapture {
    param([string[]]$Lines, [byte[]]$Bytes)
    if ($Bytes.Length -ne $gttBytes) {
        throw "Intel GTT binary is $($Bytes.Length) bytes; expected $gttBytes."
    }
    $values = ConvertFrom-V9xIntelGttIni -Lines $Lines
    if ($values['Access'] -cne 'read-only') {
        throw 'Intel GTT capture does not declare Access=read-only.'
    }
    if ($values['BarProvenance'] -cne 'PCI-BAR3-runtime') {
        throw 'Intel GTT capture does not declare runtime PCI BAR3 provenance.'
    }
    if ($values['Result'] -cne 'PASS') {
        throw "Intel GTT Phase 2 did not pass: Result=$($values['Result'])."
    }

    [uint32]$bar3 = ConvertFrom-V9xGttHex32 $values 'Bar3'
    [uint32]$gmadr = ConvertFrom-V9xGttHex32 $values 'GmadrBar2'
    [uint32]$bsm = ConvertFrom-V9xGttHex32 $values 'Bsm'
    [uint32]$ggc = ConvertFrom-V9xGttHex32 $values 'Ggc'
    [uint32]$stolen = ConvertFrom-V9xGttHex32 $values 'StolenBytes'
    [uint32]$vbe = ConvertFrom-V9xGttHex32 $values 'VbeBytes'
    [uint32]$pgtbl = ConvertFrom-V9xGttHex32 $values 'PgtblCtl'
    [uint32]$storage = ConvertFrom-V9xGttHex32 $values 'GttStorage'
    [uint32]$flags = ConvertFrom-V9xGttHex32 $values 'Flags'
    if ($bar3 -lt 0x01000000 -or ($bar3 -band 0x0003ffff) -ne 0) {
        throw ('Intel GTT BAR3 is not a plausible aligned 256-KiB window: ' +
               ('{0:X8}.' -f $bar3))
    }
    if ($gmadr -lt 0x10000000 -or ($gmadr -band 0x0fffffff) -ne 0) {
        throw "GMADR BAR2 is not 256-MiB aligned: $('{0:X8}' -f $gmadr)."
    }
    if (($bsm -band 0xfff) -ne 0 -or $stolen -lt $gttBytes -or
        ($stolen -band 0xfff) -ne 0 -or $vbe -lt $reserveBytes -or
        ($vbe -band 0xfff) -ne 0) { throw 'BSM/stolen/VBE geometry is invalid.' }
    if ((ConvertFrom-V9xGttHex32 $values 'Entries') -ne $gttEntries -or
        (ConvertFrom-V9xGttHex32 $values 'BinaryBytes') -ne $gttBytes) {
        throw 'Intel GTT text does not describe the complete 256-KiB table.'
    }
    if (($flags -band $requiredFlags) -ne $requiredFlags) {
        throw "Intel GTT capture lacks a Phase 2 relationship bit: Flags=$('{0:X8}' -f $flags)."
    }
    [uint32]$expectedStorage = [uint32]([uint64]$bsm + $stolen - $gttBytes)
    if ($storage -ne $expectedStorage -or ($pgtbl -band 1) -eq 0 -or
        ($pgtbl -band 0xfffff000) -ne $storage) {
        throw 'PGTBL_CTL does not select the inferred top-of-stolen GTT storage.'
    }
    if (((($ggc -shr 4) -band 7) -eq 0)) { throw 'GGC does not encode stolen memory.' }

    [uint32]$fnv = Get-V9xFnv1a32 $Bytes
    foreach ($key in @('HashA', 'HashB', 'HashStream')) {
        if ((ConvertFrom-V9xGttHex32 $values $key) -ne $fnv) {
            throw "$key does not match the binary FNV-1a hash."
        }
    }
    $analysis = Get-V9xGttAnalysis -Bytes $Bytes -Bsm $bsm -VbeBytes $vbe
    $countFields = @{
        Present = $analysis.Present; Uncached = $analysis.Uncached
        Local = $analysis.Local; Cached = $analysis.Cached
        UnknownAttrs = $analysis.Unknown; Runs = $analysis.Runs.Count
        BackedPrefix = $analysis.BackedPrefix
    }
    foreach ($key in $countFields.Keys) {
        if ((ConvertFrom-V9xGttHex32 $values $key) -ne [uint32]$countFields[$key]) {
            throw "$key does not match the decoded binary."
        }
    }
    if ($analysis.Unknown -ne 0) { throw 'Intel GTT contains unknown PTE attributes.' }
    if ((ConvertFrom-V9xGttHex32 $values 'RunsLogged') -ne $analysis.Runs.Count) {
        throw 'The text run map is incomplete.'
    }
    for ($index = 0; $index -lt $analysis.Runs.Count; ++$index) {
        $run = $analysis.Runs[$index]
        $prefix = 'Run{0:X4}' -f $index
        $expected = @{ S = $run.Start; N = $run.Count; P = $run.FirstPhysical
                       D = $run.Stride; A = $run.Attributes }
        foreach ($suffix in $expected.Keys) {
            if ((ConvertFrom-V9xGttHex32 $values ($prefix + $suffix)) -ne
                [uint32]$expected[$suffix]) { throw "Run map mismatch in $prefix$suffix." }
        }
    }

    [uint32]$reserveEntries = $reserveBytes / $pageBytes
    [uint32]$reserveFirst = $vbe / $pageBytes - $reserveEntries
    [uint32]$reserveOffset = $reserveFirst * $pageBytes
    [uint32]$reservePhysical = [uint32]([uint64]$bsm + $reserveOffset)
    $reserveFields = @{
        ReserveEntry = $reserveFirst; ReserveEntries = $reserveEntries
        ReserveOffset = $reserveOffset; ReservePhysical = $reservePhysical
    }
    foreach ($key in $reserveFields.Keys) {
        if ((ConvertFrom-V9xGttHex32 $values $key) -ne [uint32]$reserveFields[$key]) {
            throw "$key does not match the proposed top-of-VBE reservation."
        }
    }
    if ($analysis.BackedPrefix -lt ($vbe / $pageBytes)) {
        throw 'The VBE-reported framebuffer is not fully backed by linear PTEs.'
    }

    if ((ConvertFrom-V9xGttHex32 $values 'SampleCount') -ne 2) {
        throw 'Exactly two decoded-valid GMADR samples are required.'
    }
    foreach ($sample in @(
        @{ Name = 'Sample0'; Offset = [uint32]0; Physical = $bsm },
        @{ Name = 'SampleReserve'; Offset = $reserveOffset; Physical = $reservePhysical })) {
        [uint32]$offset = ConvertFrom-V9xGttHex32 $values ($sample.Name + 'Offset')
        [uint32]$pte = ConvertFrom-V9xGttHex32 $values ($sample.Name + 'Pte')
        $null = ConvertFrom-V9xGttHex32 $values ($sample.Name + 'Data')
        [uint32]$binaryPte = [BitConverter]::ToUInt32($Bytes, $offset / $pageBytes * 4)
        if ($offset -ne $sample.Offset -or $offset -gt $vbe - 4 -or
            $pte -ne $binaryPte -or ($pte -band 1) -eq 0 -or
            ($pte -band 0xfffff000) -ne $sample.Physical) {
            throw "$($sample.Name) was not sampled through its decoded-valid linear PTE."
        }
    }

    # Windows PowerShell 5.1 has neither SHA256.HashData nor Convert.ToHexString.
    $sha256Algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $sha256 = -join ($sha256Algorithm.ComputeHash($Bytes) |
                         ForEach-Object { $_.ToString('x2') })
    } finally { $sha256Algorithm.Dispose() }
    return [pscustomobject]@{
        Result = 'PASS'; Bar3 = '{0:X8}' -f $bar3; GmadrBar2 = '{0:X8}' -f $gmadr
        Bsm = '{0:X8}' -f $bsm; Fnv1a32 = '{0:X8}' -f $fnv; Sha256 = $sha256
        Entries = $gttEntries; Present = $analysis.Present
        BackedPrefix = $analysis.BackedPrefix; Runs = $analysis.Runs.Count
        ReserveOffset = '{0:X8}' -f $reserveOffset
        ReservePhysical = '{0:X8}' -f $reservePhysical
    }
}

if ($SelfTest) {
    $tempDirectory = Join-Path ([IO.Path]::GetTempPath()) `
        ('v9x-intel-gtt-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($tempDirectory) | Out-Null
    try {
        $bytes = [byte[]]::new($gttBytes)
        [uint32]$bsm = 0x7f800000
        [uint32]$vbe = 0x007b0000
        for ($index = 0; $index -lt $gttEntries; ++$index) {
            [uint32]$raw = if ($index -lt ($vbe / $pageBytes)) {
                [uint32]([uint64]$bsm + [uint64]$index * $pageBytes + 1)
            } else { 0x7ffb0001 }
            [BitConverter]::GetBytes($raw).CopyTo($bytes, $index * 4)
        }
        [uint32]$fnv = Get-V9xFnv1a32 $bytes
        $analysis = Get-V9xGttAnalysis $bytes $bsm $vbe
        $lines = [System.Collections.Generic.List[string]]::new()
        foreach ($line in @(
            '[IntelGtt]', 'Access=read-only', 'BarProvenance=PCI-BAR3-runtime',
            'Bar3=FEA40000', 'GmadrBar2=D0000000', 'Bsm=7F800000',
            'Ggc=00000030', 'StolenBytes=00800000', 'VbeBytes=007B0000',
            'PgtblCtl=7FFC0001', 'GttStorage=7FFC0000', 'Entries=00010000',
            'BinaryBytes=00040000', ('HashA={0:X8}' -f $fnv),
            ('HashB={0:X8}' -f $fnv), ('HashStream={0:X8}' -f $fnv),
            'Flags=0000007F', ('Present={0:X8}' -f $analysis.Present),
            ('Uncached={0:X8}' -f $analysis.Uncached),
            ('Local={0:X8}' -f $analysis.Local),
            ('Cached={0:X8}' -f $analysis.Cached), 'UnknownAttrs=00000000',
            ('Runs={0:X8}' -f $analysis.Runs.Count),
            ('RunsLogged={0:X8}' -f $analysis.Runs.Count),
            ('BackedPrefix={0:X8}' -f $analysis.BackedPrefix),
            'ReserveEntry=00000790', 'ReserveEntries=00000020',
            'ReserveOffset=00790000', 'ReservePhysical=7FF90000',
            'SampleCount=00000002', 'Sample0Offset=00000000',
            'Sample0Pte=7F800001', 'Sample0Data=00000000',
            'SampleReserveOffset=00790000', 'SampleReservePte=7FF90001',
            'SampleReserveData=00000000', 'Result=PASS')) { $lines.Add($line) }
        for ($index = 0; $index -lt $analysis.Runs.Count; ++$index) {
            $run = $analysis.Runs[$index]
            $prefix = 'Run{0:X4}' -f $index
            $lines.Add($prefix + 'S={0:X8}' -f $run.Start)
            $lines.Add($prefix + 'N={0:X8}' -f $run.Count)
            $lines.Add($prefix + 'P={0:X8}' -f $run.FirstPhysical)
            $lines.Add($prefix + 'D={0:X8}' -f $run.Stride)
            $lines.Add($prefix + 'A={0:X8}' -f $run.Attributes)
        }
        $null = Test-V9xIntelGttCapture -Lines $lines -Bytes $bytes
        foreach ($mutation in @(
            @{ Text = $true; Old = 'Result=PASS'; New = 'Result=REVIEW' },
            @{ Text = $true; Old = 'SampleReservePte=7FF90001'; New = 'SampleReservePte=7FF91001' },
            @{ Text = $false; Offset = 0 })) {
            $brokenLines = @($lines)
            $brokenBytes = [byte[]]$bytes.Clone()
            if ($mutation.Text) {
                $brokenLines = @($lines | ForEach-Object {
                    if ($_ -ceq $mutation.Old) { $mutation.New } else { $_ }
                })
            } else { $brokenBytes[$mutation.Offset] = $brokenBytes[$mutation.Offset] -bxor 1 }
            $rejected = $false
            try { $null = Test-V9xIntelGttCapture $brokenLines $brokenBytes }
            catch { $rejected = $true }
            if (-not $rejected) { throw 'Intel GTT validator self-test accepted a mutation.' }
        }
        Write-Output 'Intel GTT capture validator self-test passed (clean accepted, 3 mutations rejected).'
    } finally {
        if ([IO.Directory]::Exists($tempDirectory)) {
            [IO.Directory]::Delete($tempDirectory, $true)
        }
    }
    exit 0
}

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Intel GTT capture text does not exist: $Path"
}
if (-not $BinaryPath) {
    $BinaryPath = [IO.Path]::ChangeExtension((Resolve-Path -LiteralPath $Path).Path, '.BIN')
}
if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) {
    throw "Intel GTT capture binary does not exist: $BinaryPath"
}
$capture = Test-V9xIntelGttCapture -Lines @(Get-Content -LiteralPath $Path) `
    -Bytes ([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $BinaryPath).Path))
if ($Json) { $capture | ConvertTo-Json -Compress } else { $capture | Format-List }
