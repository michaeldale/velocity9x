# Validate the Intel Gen3 Phase 3 firmware-ownership event matrix.
[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [string]$Path,
    [Parameter(ParameterSetName = 'Capture')]
    [string]$ExpectedInitialGttHash,
    [Parameter(ParameterSetName = 'Capture')]
    [switch]$Json,
    [Parameter(Mandatory = $true, ParameterSetName = 'SelfTest')]
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$requiredFlags = 0x3f
$requiredCoverage = 0x1d
$recordDwords = 20
$fields = @('Sequence', 'Kind', 'Context', 'Flags', 'PgtblCtl', 'RingTail',
    'RingHead', 'RingStart', 'RingCtl', 'HwsPga', 'Fence0', 'Fence1',
    'Fence2', 'Fence3', 'Fence4', 'Fence5', 'Fence6', 'Fence7',
    'GttHashA', 'GttHashB')
$ownershipFields = @('PgtblCtl', 'RingTail', 'RingHead', 'RingStart', 'RingCtl',
    'HwsPga', 'Fence0', 'Fence1', 'Fence2', 'Fence3', 'Fence4', 'Fence5',
    'Fence6', 'Fence7', 'GttHashA')

function ConvertFrom-V9xEventIni {
    param([string[]]$Lines)
    $sections = @{}
    $current = $null
    foreach ($line in $Lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            $current = $Matches[1]
            if ($sections.ContainsKey($current)) {
                throw "Intel event capture repeats section $current."
            }
            $sections[$current] = @{}
            continue
        }
        if ($null -eq $current -or -not $trimmed -or
            $trimmed.StartsWith(';') -or $trimmed.StartsWith('#')) { continue }
        $separator = $trimmed.IndexOf('=')
        if ($separator -le 0) { continue }
        $key = $trimmed.Substring(0, $separator).Trim()
        if ($sections[$current].ContainsKey($key)) {
            throw "Intel event section $current repeats key $key."
        }
        $sections[$current][$key] = $trimmed.Substring($separator + 1).Trim()
    }
    return $sections
}

function ConvertFrom-V9xEventHex {
    param([hashtable]$Values, [string]$Key, [string]$Where)
    if (-not $Values.ContainsKey($Key) -or
        $Values[$Key] -notmatch '^[0-9A-Fa-f]{8}$') {
        throw "$Where is missing an eight-digit hexadecimal $Key."
    }
    return [Convert]::ToUInt32($Values[$Key], 16)
}

function Test-V9xIntelEventCapture {
    param([string[]]$Lines, [string]$InitialHash)
    $sections = ConvertFrom-V9xEventIni $Lines
    if (-not $sections.ContainsKey('IntelEvents')) {
        throw 'Intel event capture has no [IntelEvents] section.'
    }
    $header = $sections['IntelEvents']
    if ($header['Access'] -cne 'read-only') {
        throw 'Intel event capture does not declare Access=read-only.'
    }
    if ($header['Result'] -cne 'READY') {
        throw "Intel Phase 3 matrix is not ready: Result=$($header['Result'])."
    }
    [uint32]$count = ConvertFrom-V9xEventHex $header 'Count' 'IntelEvents'
    [uint32]$dropped = ConvertFrom-V9xEventHex $header 'Dropped' 'IntelEvents'
    [uint32]$declaredDwords = ConvertFrom-V9xEventHex $header 'RecordDwords' 'IntelEvents'
    [uint32]$declaredCoverage = ConvertFrom-V9xEventHex $header 'Coverage' 'IntelEvents'
    if ($count -eq 0 -or $count -gt 32 -or $dropped -ne 0 -or
        $declaredDwords -ne $recordDwords) {
        throw 'Intel event journal is empty, truncated, dropped, or has the wrong record size.'
    }

    $records = @()
    [uint32]$coverage = 0
    for ($index = 0; $index -lt $count; ++$index) {
        $name = 'IntelEvent{0:X2}' -f $index
        if (-not $sections.ContainsKey($name)) { throw "Missing event section $name." }
        $values = $sections[$name]
        $record = [ordered]@{}
        foreach ($field in $fields) {
            $record[$field] = ConvertFrom-V9xEventHex $values $field $name
        }
        if ($record.Sequence -ne [uint32]($index + 1)) {
            throw "$name has a non-monotonic sequence number."
        }
        if (($record.Flags -band $requiredFlags) -ne $requiredFlags) {
            throw "$name is not a stable, disabled, idle ownership snapshot."
        }
        if ($record.GttHashA -ne $record.GttHashB) {
            throw "$name changed GTT contents between its two full reads."
        }
        if (($record.RingTail -band 0x001ffff8) -ne
            ($record.RingHead -band 0x001ffff8) -or
            ($record.RingCtl -band 1) -ne 0 -or
            ($record.PgtblCtl -band 1) -eq 0) {
            throw "$name flags disagree with its ring or PGTBL values."
        }
        switch ($record.Kind) {
        1 { $coverage = $coverage -bor 0x01 }
        2 { $coverage = $coverage -bor 0x02 }
        3 { $coverage = $coverage -bor 0x04 }
        4 { $coverage = $coverage -bor 0x08 }
        5 { $coverage = $coverage -bor 0x10 }
        6 {
            $coverage = $coverage -bor $(if ($record.Context -eq 0) { 0x20 } else { 0x40 })
        }
        default { throw "$name has unknown event kind $($record.Kind)." }
        }
        $records += [pscustomobject]$record
    }
    if (($coverage -band $requiredCoverage) -ne $requiredCoverage -or
        $declaredCoverage -ne $coverage) {
        throw ('Intel event matrix lacks boot, disable, mode-switch or ' +
               'mode-restore coverage.')
    }
    if ($InitialHash) {
        if ($InitialHash -notmatch '^[0-9A-Fa-f]{8}$') {
            throw 'ExpectedInitialGttHash must be eight hexadecimal digits.'
        }
        [uint32]$expected = [Convert]::ToUInt32($InitialHash, 16)
        if ($records[0].GttHashA -ne $expected) {
            throw 'The boot event GTT hash does not match the Phase 2 baseline.'
        }
    }

    $changes = @()
    for ($index = 1; $index -lt $records.Count; ++$index) {
        foreach ($field in $ownershipFields) {
            if ($records[$index].$field -ne $records[$index - 1].$field) {
                $changes += ('{0:X2}->{1:X2}:{2}:{3:X8}->{4:X8}' -f
                    ($index - 1), $index, $field,
                    $records[$index - 1].$field, $records[$index].$field)
            }
        }
    }
    return [pscustomobject]@{
        Result = 'PASS'; Events = $records.Count
        Coverage = '{0:X8}' -f $coverage; Dropped = $dropped
        OwnershipChanges = $changes.Count
        Changes = if ($changes.Count -eq 0) { @('none') } else { $changes }
        InitialGttHash = '{0:X8}' -f $records[0].GttHashA
        FinalGttHash = '{0:X8}' -f $records[-1].GttHashA
    }
}

if ($SelfTest) {
    $kinds = @(
        @{ Kind = 1; Context = 0x0161 }, @{ Kind = 4; Context = 0x0111 },
        @{ Kind = 3; Context = 0x0111 }, @{ Kind = 2; Context = 0x0111 },
        @{ Kind = 6; Context = 0x0050 }, @{ Kind = 6; Context = 0 },
        @{ Kind = 5; Context = 0x0111 })
    $lines = @('[IntelEvents]', 'Access=read-only',
        ('Count={0:X8}' -f $kinds.Count), 'Dropped=00000000',
        'RecordDwords=00000014', 'Coverage=0000007F', 'Result=READY')
    for ($index = 0; $index -lt $kinds.Count; ++$index) {
        $lines += '[IntelEvent{0:X2}]' -f $index
        $values = @(($index + 1), $kinds[$index].Kind, $kinds[$index].Context,
            0x3f, 0x7ffc0001, 0, 0, 0, 0, 0x1ffff000,
            0, 0, 0, 0, 0, 0, 0, 0, 0x4d8707c5, 0x4d8707c5)
        for ($field = 0; $field -lt $fields.Count; ++$field) {
            $lines += $fields[$field] + '={0:X8}' -f $values[$field]
        }
        $lines += 'KindText=test'
    }
    $null = Test-V9xIntelEventCapture $lines '4D8707C5'
    foreach ($replacement in @(
        @{ From = 'Result=READY'; To = 'Result=CAPTURED' },
        @{ From = 'Dropped=00000000'; To = 'Dropped=00000001' },
        @{ From = 'GttHashB=4D8707C5'; To = 'GttHashB=4D8707C4' })) {
        $broken = @($lines | ForEach-Object {
            if ($_ -ceq $replacement.From) { $replacement.To } else { $_ }
        })
        $rejected = $false
        try { $null = Test-V9xIntelEventCapture $broken '4D8707C5' }
        catch { $rejected = $true }
        if (-not $rejected) { throw 'Intel event validator accepted a broken fixture.' }
    }
    Write-Output 'Intel event capture validator self-test passed (clean accepted, 3 mutations rejected).'
    exit 0
}

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Intel event capture does not exist: $Path"
}
$result = Test-V9xIntelEventCapture @(Get-Content -LiteralPath $Path) `
    $ExpectedInitialGttHash
if ($Json) { $result | ConvertTo-Json -Depth 4 -Compress } else { $result | Format-List }
