# Velocity9x family-manifest loader.
#
# Dot-source this from a build script. It reads packaging\families\*\family.psd1
# with Import-PowerShellDataFile (built into PowerShell 5.1, evaluates no code),
# validates the schema, and derives the cross-family audit rules that keep one
# family's chip code out of another family's binary.
#
# See docs\plans\multi-chip-restructure.md and, once written,
# docs\specifications\family-manifest.md.

$script:V9xFamilySchemaVersion = 1

# The engine vocabulary, spelled without the V9X_DD_ENGINE_TYPE_ /
# V9X_DD_ENGINE_CAP_ prefix that include\velocity9x\engine_abi.h gives it. The
# generated host family matrix pastes the prefix back on, so a name that is not
# in this list would become a compile error rather than a silent zero - but
# catching it here names the manifest and the chip instead.
$script:V9xEngineTypes = @('NONE', 'S3_VIRGE_DX', 'S3_TRIO64')
$script:V9xEngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'VBLANK', 'D3D')

function Get-V9xFamilyRoot {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)
    Join-Path $RepoRoot "packaging\families"
}

function Get-V9xFamilyIds {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    $root = Get-V9xFamilyRoot -RepoRoot $RepoRoot
    if (-not (Test-Path -LiteralPath $root)) {
        throw "No family manifests directory at $root."
    }
    @(Get-ChildItem -LiteralPath $root -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "family.psd1") } |
        ForEach-Object { $_.Name } | Sort-Object)
}

function Assert-V9xFamilyKeys {
    param(
        [Parameter(Mandatory = $true)]$Table,
        [Parameter(Mandatory = $true)][string[]]$Required,
        [Parameter(Mandatory = $true)][string]$Context
    )

    if ($Table -isnot [hashtable]) {
        throw "$Context must be a hashtable."
    }
    $missing = @($Required | Where-Object { -not $Table.ContainsKey($_) })
    if ($missing.Count -ne 0) {
        throw "$Context is missing key(s): $($missing -join ', ')."
    }
}

function Test-V9xFamilyManifest {
    param(
        [Parameter(Mandatory = $true)]$Family,
        [Parameter(Mandatory = $true)][string]$Id
    )

    Assert-V9xFamilyKeys -Table $Family -Context "Family $Id" -Required @(
        'SchemaVersion', 'Id', 'DisplayName', 'Description',
        'Chips', 'Build', 'Audit', 'Inf', 'Floppy', 'Vm')
    if ($Family.SchemaVersion -ne $script:V9xFamilySchemaVersion) {
        throw ("Family $Id declares schema version {0}; this loader understands {1}." -f
               $Family.SchemaVersion, $script:V9xFamilySchemaVersion)
    }
    if ($Family.Id -ne $Id) {
        throw "Family in packaging\families\$Id declares Id '$($Family.Id)'."
    }
    if ($Family.Id -notmatch '^[a-z0-9]+(-[a-z0-9]+)*$') {
        throw "Family id '$($Family.Id)' must be lowercase kebab-case."
    }

    $chips = @($Family.Chips)
    if ($chips.Count -eq 0) {
        throw "Family $Id declares no chips."
    }
    foreach ($chip in $chips) {
        Assert-V9xFamilyKeys -Table $chip -Context "Family $Id chip" -Required @(
            'Id', 'Name', 'VendorId', 'DeviceId', 'DeviceDesc', 'Modes', 'Audit',
            'EngineType', 'EngineCaps', 'VideoMemoryBytes')
        if ($chip.EngineType -notin $script:V9xEngineTypes) {
            throw ("Family $Id chip $($chip.Id) declares unknown EngineType " +
                   "'$($chip.EngineType)'. Known: " +
                   ($script:V9xEngineTypes -join ', '))
        }
        foreach ($cap in @($chip.EngineCaps)) {
            if ($cap -notin $script:V9xEngineCaps) {
                throw ("Family $Id chip $($chip.Id) declares unknown EngineCap " +
                       "'$cap'. Known: " + ($script:V9xEngineCaps -join ', '))
            }
        }
        # A chip with no engine cannot have engine capabilities. This is the
        # rule the 32-bit HAL relies on: it resolves no ops table for
        # V9X_DD_ENGINE_TYPE_NONE, so anything the manifest claimed there would
        # be advertised and then never served.
        if ($chip.EngineType -eq 'NONE' -and @($chip.EngineCaps).Count -ne 0) {
            throw ("Family $Id chip $($chip.Id) has EngineType NONE but claims " +
                   "capabilities: $(@($chip.EngineCaps) -join ', ').")
        }
        if ([uint32]$chip.VideoMemoryBytes -eq 0) {
            throw "Family $Id chip $($chip.Id) declares no VideoMemoryBytes."
        }
        foreach ($field in @('VendorId', 'DeviceId')) {
            if ($chip.$field -notmatch '^[0-9A-F]{4}$') {
                throw ("Family $Id chip $($chip.Id) has a non-canonical $field " +
                       "'$($chip.$field)'; use four uppercase hex digits.")
            }
        }
        if (@($chip.Modes).Count -eq 0) {
            throw "Family $Id chip $($chip.Id) declares no modes."
        }
        foreach ($mode in @($chip.Modes)) {
            Assert-V9xFamilyKeys -Table $mode -Required @(
                'BitsPerPixel', 'Width', 'Height', 'RefreshRate') `
                -Context "Family $Id chip $($chip.Id) mode"
        }
        Assert-V9xFamilyKeys -Table $chip.Audit -Required @('Required', 'Forbidden') `
            -Context "Family $Id chip $($chip.Id) Audit"
        foreach ($pattern in (@($chip.Audit.Required) + @($chip.Audit.Forbidden))) {
            try {
                $null = [regex]::new($pattern)
            } catch {
                throw "Family $Id chip $($chip.Id) has invalid audit pattern '$pattern': $($_.Exception.Message)"
            }
        }
    }

    Assert-V9xFamilyKeys -Table $Family.Build -Context "Family $Id Build" -Required @(
        'Sources', 'Defines', 'RuntimeDefines', 'SkeletonOutput', 'PackageOutput')
    # Optional: absent means the mini-VDD keeps its boot-time VBE collection,
    # which is what tier-0 families (no read_aperture hook) require.
    if ($Family.Build.ContainsKey('MiniVddVbeCollect') -and
        $Family.Build.MiniVddVbeCollect -isnot [bool]) {
        throw "Family $Id Build.MiniVddVbeCollect must be a boolean when present."
    }
    $sources = @($Family.Build.Sources)
    if ($sources.Count -eq 0) {
        throw "Family $Id declares no build sources."
    }
    $sourceNames = @($sources | ForEach-Object { $_.Name })
    $duplicateNames = @($sourceNames | Group-Object |
        Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
    if ($duplicateNames.Count -ne 0) {
        throw "Family $Id repeats object name(s): $($duplicateNames -join ', ')."
    }

    # Chips[].Objects is what makes the per-object audit layer possible: a
    # multi-chip image checked only as a whole cannot tell one sibling's code
    # from another's. Optional, because a single-chip family gains nothing from
    # it - but a name that does not resolve to a real object would make the
    # audit silently skip the chip, so it is validated when present.
    foreach ($chip in @($Family.Chips)) {
        if (-not $chip.ContainsKey('Objects')) {
            continue
        }
        $unknown = @(@($chip.Objects | Where-Object { $_ }) |
            Where-Object { $_ -notin $sourceNames })
        if ($unknown.Count -ne 0) {
            throw ("Family $Id chip $($chip.Id) names object(s) the build does " +
                   "not produce: $($unknown -join ', ').")
        }
    }
    # A family whose chips all declare objects gets the per-object layer; one
    # that mixes declared and undeclared would audit some chips and not others,
    # which reads as coverage it does not have.
    $withObjects = @(@($Family.Chips) | Where-Object { $_.ContainsKey('Objects') }).Count
    if ($withObjects -ne 0 -and $withObjects -ne @($Family.Chips).Count) {
        throw ("Family $Id declares Objects for $withObjects of " +
               "$(@($Family.Chips).Count) chips; declare it for all or none.")
    }

    Assert-V9xFamilyKeys -Table $Family.Audit -Context "Family $Id Audit" -Required @(
        'RequiredInstructions', 'ForbiddenInstructions', 'RequiredMapSymbols')
    Assert-V9xFamilyKeys -Table $Family.Floppy -Context "Family $Id Floppy" -Required @(
        'Include', 'Folder')
    if ($Family.Floppy.Include -and -not $Family.Floppy.Folder) {
        throw "Family $Id is on the floppy but names no folder."
    }
    Assert-V9xFamilyKeys -Table $Family.Vm -Context "Family $Id Vm" -Required @(
        'Emulator', 'Profile', 'Port', 'Modes')
    if ($Family.Vm.Emulator -ne 'none') {
        if (-not $Family.Vm.Profile -or [int]$Family.Vm.Port -le 0) {
            throw "Family $Id declares emulator '$($Family.Vm.Emulator)' but no profile/port."
        }
    }
    # One VM target per chip. A multi-chip family that declared only the
    # primary would let a mode-matrix pass on one chip read as a pass for the
    # family, which is exactly the claim the merge has to prove.
    if ($Family.Vm.ContainsKey('Targets')) {
        $chipIds = @(@($Family.Chips) | ForEach-Object { $_.Id })
        foreach ($vmTarget in @($Family.Vm.Targets)) {
            # A per-target Emulator overrides the family's, and 'none' means
            # this chip is real hardware only. That is not hypothetical: the
            # ati family pairs a Mach64 VT2 that 86Box emulates with a Rage
            # Mobility that nothing does, and demanding a profile and port for
            # the Mobility would mean inventing a guest that cannot exist.
            # Absent the key a target inherits the family emulator, so every
            # existing manifest behaves exactly as before.
            $targetEmulator = $Family.Vm.Emulator
            if ($vmTarget.ContainsKey('Emulator')) {
                $targetEmulator = $vmTarget.Emulator
            }
            if ($targetEmulator -eq 'none') {
                Assert-V9xFamilyKeys -Table $vmTarget -Required @('ChipId') `
                    -Context "Family $Id Vm target"
            } else {
                Assert-V9xFamilyKeys -Table $vmTarget -Required @('ChipId', 'Profile', 'Port') `
                    -Context "Family $Id Vm target"
            }
            if ($vmTarget.ChipId -notin $chipIds) {
                throw ("Family $Id declares a VM target for unknown chip " +
                       "'$($vmTarget.ChipId)'.")
            }
        }
        $covered = @(@($Family.Vm.Targets) | ForEach-Object { $_.ChipId } | Sort-Object -Unique)
        $uncovered = @($chipIds | Where-Object { $_ -notin $covered })
        if ($uncovered.Count -ne 0) {
            throw ("Family $Id declares VM targets but none for chip(s): " +
                   "$($uncovered -join ', ').")
        }
    } elseif (@($Family.Chips).Count -gt 1 -and $Family.Vm.Emulator -ne 'none') {
        throw ("Family $Id has $(@($Family.Chips).Count) chips and an emulator " +
               "but declares no Vm.Targets, so its mode matrix could only ever " +
               "cover one of them.")
    }
}

# The VM target for one chip, or the family's primary when it declares none.
function Get-V9xFamilyVmTarget {
    param(
        [Parameter(Mandatory = $true)]$Family,
        [string]$ChipId
    )

    # Filter the nulls rather than trusting Count: @($null) has one element in
    # PowerShell, so a family declaring no Vm.Targets - which a single-chip
    # family is not required to - would fall through to the per-target branch
    # and dereference $null. vbe was the first family with none.
    $targets = @($Family.Vm.Targets | Where-Object { $_ })
    if ($targets.Count -eq 0) {
        return [pscustomobject]@{
            ChipId = @(@($Family.Chips) | ForEach-Object { $_.Id })[0]
            Profile = $Family.Vm.Profile
            Port = [int]$Family.Vm.Port
        }
    }
    if (-not $ChipId) {
        $match = @($targets | Where-Object { $_.Profile -eq $Family.Vm.Profile })
        if ($match.Count -eq 0) { $match = @($targets) }
        $chosen = $match[0]
    } else {
        $chosen = @($targets | Where-Object { $_.ChipId -eq $ChipId })
        if ($chosen.Count -ne 1) {
            $known = (@($targets | ForEach-Object { $_.ChipId })) -join ', '
            throw "Family $($Family.Id) has no VM target for chip '$ChipId'. Known: $known."
        }
        $chosen = $chosen[0]
    }
    $chosenEmulator = $Family.Vm.Emulator
    if ($chosen.ContainsKey('Emulator')) {
        $chosenEmulator = $chosen.Emulator
    }
    [pscustomobject]@{
        ChipId = $chosen.ChipId
        Profile = $chosen.Profile
        Port = [int]$chosen.Port
        Emulator = $chosenEmulator
    }
}

# Every chip a family's mode matrix has to cover.
function Get-V9xFamilyVmChipIds {
    param([Parameter(Mandatory = $true)]$Family)
    # Filter the nulls rather than trusting Count: @($null) has one element in
    # PowerShell, so a family declaring no Vm.Targets - which a single-chip
    # family is not required to - would fall through to the per-target branch
    # and dereference $null. vbe was the first family with none.
    $targets = @($Family.Vm.Targets | Where-Object { $_ })
    if ($targets.Count -eq 0) {
        return @(@(@($Family.Chips) | ForEach-Object { $_.Id })[0])
    }
    @($targets | ForEach-Object { $_.ChipId })
}

function Import-V9xFamily {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$Id
    )

    $path = Join-Path (Get-V9xFamilyRoot -RepoRoot $RepoRoot) "$Id\family.psd1"
    if (-not (Test-Path -LiteralPath $path)) {
        $known = (Get-V9xFamilyIds -RepoRoot $RepoRoot) -join ', '
        throw "Unknown family '$Id'. Known families: $known."
    }
    $family = Import-PowerShellDataFile -LiteralPath $path
    Test-V9xFamilyManifest -Family $family -Id $Id
    $family['ManifestPath'] = $path
    $family
}

function Get-V9xFamilies {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    $families = @(Get-V9xFamilyIds -RepoRoot $RepoRoot | ForEach-Object {
        Import-V9xFamily -RepoRoot $RepoRoot -Id $_
    })

    # A PCI ID may name exactly one family. Two families claiming the same
    # device would give Windows two matching INF models and make which driver
    # installs a coin toss.
    $owners = @{}
    foreach ($family in $families) {
        foreach ($chip in @($family.Chips)) {
            $key = "{0}:{1}" -f $chip.VendorId, $chip.DeviceId
            if ($owners.ContainsKey($key)) {
                throw ("PCI ID $key is claimed by both family " +
                       "'$($owners[$key])' and family '$($family.Id)'.")
            }
            $owners[$key] = $family.Id
        }
    }
    $families
}

function Get-V9xFamilyHardwareIds {
    param([Parameter(Mandatory = $true)]$Family)
    @(@($Family.Chips) | ForEach-Object {
        "PCI\VEN_{0}&DEV_{1}" -f $_.VendorId, $_.DeviceId
    } | Sort-Object -Unique)
}

function Get-V9xFamilyRequiredPatterns {
    param([Parameter(Mandatory = $true)]$Family)
    @(@(@($Family.Chips) | ForEach-Object { @($_.Audit.Required) }) +
      @($Family.Audit.RequiredInstructions)) | Sort-Object -Unique
}

# Every other family's required signatures become this family's forbidden set,
# minus anything this family legitimately produces. Adding a family therefore
# strengthens every existing family's audit with no script edit.
function Get-V9xFamilyForbiddenPatterns {
    param(
        [Parameter(Mandatory = $true)]$Family,
        [Parameter(Mandatory = $true)]$AllFamilies
    )

    $own = @(Get-V9xFamilyRequiredPatterns -Family $Family)
    $foreign = @(@($AllFamilies) |
        Where-Object { $_.Id -ne $Family.Id } |
        ForEach-Object { Get-V9xFamilyRequiredPatterns -Family $_ })
    @(@($foreign + @($Family.Audit.ForbiddenInstructions) +
        @(@($Family.Chips) | ForEach-Object { @($_.Audit.Forbidden) })) |
        Where-Object { $_ -notin $own } | Sort-Object -Unique)
}

# Within a family, one chip's object must not carry a sibling chip's
# signatures. Used by the per-chip-object audit layer from phase 8 on.
function Get-V9xChipForbiddenPatterns {
    param(
        [Parameter(Mandatory = $true)]$Family,
        [Parameter(Mandatory = $true)]$Chip,
        [Parameter(Mandatory = $true)]$AllFamilies
    )

    $own = @($Chip.Audit.Required)
    $siblings = @(@($Family.Chips) |
        Where-Object { $_.Id -ne $Chip.Id } |
        ForEach-Object { @($_.Audit.Required) })
    @(@($siblings +
        @(Get-V9xFamilyForbiddenPatterns -Family $Family -AllFamilies $AllFamilies)) |
        Where-Object { $_ -notin $own } | Sort-Object -Unique)
}

function Get-V9xFamilyModeRegistryEntries {
    param([Parameter(Mandatory = $true)]$Chip)
    @(@($Chip.Modes) | ForEach-Object {
        [pscustomobject]@{
            Key = "MODES\{0}\{1},{2}" -f $_.BitsPerPixel, $_.Width, $_.Height
            RefreshRate = $_.RefreshRate
        }
    })
}
