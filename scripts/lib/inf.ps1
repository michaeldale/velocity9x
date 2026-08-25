# Structured Chicago INF generation for Velocity9x family packages.
#
# Replaces the string-replacement rewrite of packaging\win98se\velocity9x.inf.
# The generator is multi-model by construction: one [<Models>] line and one
# install section per chip, with shared copy/registry sections. A single-chip
# family therefore produces exactly the file the rewrite produced, which is
# what the phase 3 golden compare checks.
#
# Two artefacts of the old rewrite are reproduced deliberately and are not
# bugs in this generator:
#   * the two header comment lines still name the ViRGE/DX, because the old
#     path copied them from the checked-in INF for every target;
#   * [Strings] still carries Provider/Manufacturer/DeviceDesc/DiskName even
#     though every reference to them has been expanded inline.
# Both go away with the multi-model INF at phase 8 of
# docs\plans\multi-chip-restructure.md, together with the checked-in INF.

$script:V9xSettingsPageClsid = '{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1}'

function Get-V9xInfModeLines {
    param([Parameter(Mandatory = $true)]$Modes)

    # INF order is depth, then width, then height - not the ddi.c mode-table
    # order, which puts 640x400 last so GDI enumerates it after the other
    # 8-bpp modes.
    $ordered = @($Modes | Sort-Object `
        @{ Expression = { [int]$_.BitsPerPixel } },
        @{ Expression = { [int]$_.Width } },
        @{ Expression = { [int]$_.Height } })
    @($ordered | ForEach-Object {
        'HKR,"MODES\{0}\{1},{2}",,,{3}' -f $_.BitsPerPixel, $_.Width,
            $_.Height, $_.RefreshRate
    })
}

# The non-blank lines of one INF section, without its header. Used by the
# assertions below, which have to reason about what a section does and does not
# contain rather than only about substrings of the whole text.
# The manual-select model line, or $null when the family declares none. One
# definition so the emitter and Assert-V9xInf cannot disagree about it.
#
# No hardware id in either form: with a CompatibleId the second field is left
# empty and the id goes in the third, which is what every S3 model in Win95's
# MSDISP.INF does for a device the enumerator detects itself.
function Get-V9xInfManualModelLine {
    param([Parameter(Mandatory = $true)]$Family)

    if ($Family.Inf -isnot [hashtable] -or -not $Family.Inf.ContainsKey('ManualSelect')) {
        return $null
    }
    $manual = $Family.Inf.ManualSelect
    if ($manual.CompatibleId) {
        return '"{0}"=Velocity9x.Install.Manual,, {1}' -f $manual.Description,
            $manual.CompatibleId
    }
    '"{0}"=Velocity9x.Install.Manual' -f $manual.Description
}

function Get-V9xInfSectionBody {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string[]]$Lines,
        [Parameter(Mandatory = $true)][string]$Section
    )

    $body = @()
    $inSection = $false
    foreach ($line in $Lines) {
        if ($line -match '^\s*\[(.+)\]\s*$') {
            $inSection = ($matches[1] -eq $Section)
            continue
        }
        if ($inSection -and $line.Trim()) { $body += $line }
    }
    $body
}

function New-V9xInfText {
    param(
        [Parameter(Mandatory = $true)]$Family,
        [Parameter(Mandatory = $true)][string]$DefaultMode
    )

    $inf = $Family.Inf
    $chips = @($Family.Chips)
    $models = $inf.ModelsSection
    if (-not $models) { $models = 'Velocity9x.Models' }

    # The optional manual-select model: one models line with no hardware ID,
    # for a card on a bus Windows does not enumerate. See the manifest spec.
    $manual = $null
    $manualModes = @()
    if ($inf.ContainsKey('ManualSelect')) {
        $manual = $inf.ManualSelect
        $manualModes = @(Get-V9xFamilyManualSelectModes -Family $Family)
    }
    # A family with a manual model gets per-chip registry sections even at one
    # chip: the manual model AddRegs the shared section too, so a chip's full
    # list inlined there would leak modes the manual model must not offer.
    $perChipRegistry = ($chips.Count -gt 1 -or $manual)

    # ManualSelect.MiniVdd = $false moves DEFAULT,minivdd out of the shared
    # registry section and into the per-chip ones, so the manual model - which
    # AddRegs the shared section - ends up with no mini-VDD at all.
    #
    # A Win9x display devnode is started by the VDD loading the mini-VDD named
    # there. On the 486's Win95 4.00.950 ours does not load, so the devnode
    # never reached DN_STARTED, Device Manager reported Code 24, Display
    # Properties then offered no modes, and the desktop stayed on the 4-bpp
    # vga.drv row. Measured on 2026-08-22: clearing that one value took the
    # devnode to Problem 0 and the driver to enable-ok.
    #
    # Done by omission rather than by writing an empty value after the shared
    # section, so it does not depend on SetupX applying AddReg sections
    # left to right. The per-chip sections are guaranteed to exist here because
    # declaring ManualSelect forces them above.
    $manualMiniVdd = -not ($manual -and $manual.ContainsKey('MiniVdd') -and
                           $manual.MiniVdd -eq $false)

    # The header used to name one hardcoded adapter, which stopped being true
    # the moment a family carried two chips. It is generated from the manifest
    # now, so it cannot drift from the models below.
    $lines = @(
        '; Velocity9x Windows 98SE bring-up package'
        ('; Family {0}: {1}' -f $Family.Id, $Family.DisplayName)
    ) + @($chips | ForEach-Object {
        '; Supported adapter: {0}, PCI {1}:{2}' -f $_.Name, $_.VendorId, $_.DeviceId
    }) + @(if ($manual) {
        '; Manual-select model, no hardware ID: {0} ({1} modes within {2} bytes)' -f
            $manual.Description, $manualModes.Count, $manual.VideoMemoryBytes
    }) + @(
        ''
        '[Version]'
        'Signature="$CHICAGO$"'
        'Class=DISPLAY'
        ('Provider="{0}"' -f $inf.Provider)
        ''
        '[DestinationDirs]'
        'DefaultDestDir=11'
        'Velocity9x.Copy=11'
        ''
        '[SourceDisksNames]'
        ('1="{0}",,0' -f $inf.DiskName)
        ''
        '[SourceDisksFiles]'
        'v9xdisp.drv=1'
        'v9xmini.vxd=1'
        'v9xsetp.dll=1'
        'v9xhal.dll=1'
        ''
        '[Manufacturer]'
        ('{0}={1}' -f $inf.Manufacturer, $models)
        ''
        ("[{0}]" -f $models)
    )

    # One model line per chip. Single-chip families get one install section
    # named Velocity9x.Install, which is the name the old rewrite produced;
    # multi-chip families suffix it with the chip id.
    $installSections = @{}
    foreach ($chip in $chips) {
        $section = if ($chips.Count -eq 1) {
            'Velocity9x.Install'
        } else {
            'Velocity9x.Install.{0}' -f $chip.Id
        }
        $installSections[$chip.Id] = $section
        # Windows 98's Have Disk matches model lines against the devnode's
        # HardwareIDs, which on real machines are SUBSYS-qualified; the bare
        # VEN&DEV id lives only in CompatibleIDs and is not always consulted.
        # A chip that declares SubsystemId therefore leads with the qualified
        # id and keeps the bare id as the compatible-id field.
        if ($chip.SubsystemId) {
            $lines += '"{0}"={1},PCI\VEN_{2}&DEV_{3}&SUBSYS_{4},PCI\VEN_{2}&DEV_{3}' -f
                $chip.DeviceDesc, $section, $chip.VendorId, $chip.DeviceId,
                $chip.SubsystemId
        } else {
            $lines += '"{0}"={1},PCI\VEN_{2}&DEV_{3}' -f $chip.DeviceDesc, $section,
                $chip.VendorId, $chip.DeviceId
        }
    }

    # The manual model line carries no hardware id, which is Windows' own
    # pattern for a manual-select display model and what lets SetupX offer this
    # entry over a device nothing here claims by hardware id. With a
    # CompatibleId the id goes in the third field and the second is left empty,
    # so a device the enumerator detects itself can be re-bound to this model
    # after a reboot. Deliberately not %token%: an unresolved token is what
    # Assert-V9xInf looks for.
    if ($manual) {
        $lines += Get-V9xInfManualModelLine -Family $Family
    }

    foreach ($chip in $chips) {
        $section = $installSections[$chip.Id]
        $addReg = if (-not $perChipRegistry) {
            'AddReg=Velocity9x.Registry'
        } else {
            'AddReg=Velocity9x.Registry,Velocity9x.Registry.{0}' -f $chip.Id
        }
        $lines += @(
            ''
            ("[{0}]" -f $section)
            'CopyFiles=Velocity9x.Copy'
            'DelReg=Velocity9x.Previous'
            $addReg
        )
    }

    if ($manual) {
        $lines += @(
            ''
            '[Velocity9x.Install.Manual]'
            'CopyFiles=Velocity9x.Copy'
            'DelReg=Velocity9x.Previous'
            'AddReg=Velocity9x.Registry,Velocity9x.Registry.Manual'
            # The resources the Configuration Manager cannot discover for
            # itself. A PCI model needs none of this: the bus reports what the
            # card decodes. A model with no hardware ID sits on a device the
            # enumerator only knows exists, so with nothing declared here the
            # devnode has no resources and Device Manager reports it as not
            # present - code 24, measured on the 486 on 2026-08-22. Every
            # display model in Win95's own MSDISP.INF carries this, all twenty
            # of them, pointing at one shared VGA.LogConfig.
            'LogConfig=Velocity9x.LogConfig'
        )
    }

    $lines += @(
        ''
        '[Velocity9x.Copy]'
        'v9xdisp.drv,,,12'
        'v9xmini.vxd,,,12'
        'v9xsetp.dll,,,12'
        'v9xhal.dll,,,12'
        ''
        '[Velocity9x.Previous]'
        'HKR,,Ver'
        'HKR,,DevLoader'
        'HKR,DEFAULT'
        'HKR,MODES'
        # The volatile CURRENT key is Windows' to create; only remove a stale
        # one left by a previous install.
        'HKR,CURRENT'
        # The settings-page registration is deliberately NOT deleted here.
        # These are the same keys Velocity9x.Registry adds below, and SetupX
        # applied this DelReg after that AddReg on BARRY: the handler key and
        # the Approved value were removed by the install that had just written
        # them, while HKCR\CLSID survived only because Win9x cannot delete a
        # key that still has a subkey. The result was a machine with the DLL
        # installed, the CLSID registered, and no tab. Cleaning up after a
        # future CLSID change - the reason these lines existed - would need the
        # old CLSID anyway, which these literal paths do not carry.
        ''
        '[Velocity9x.Registry]'
        'HKR,,Ver,,4.0'
        'HKR,,DevLoader,,*vdd'
    ) + @(if ($Family.Build.MiniVddVbeCollect -ne $false) {
        # The synchronizer's marker: V9xSyncModes writes only to the display
        # class instance whose V9xFamily matches the inventory's family, and
        # refuses when zero or several instances carry it. Scan-enabled
        # families only, like the Run entry below: a family that assembles
        # collection out publishes exactly its INF baseline and gets no
        # per-boot synchronizer to keep that true.
        ('HKR,,V9xFamily,,"{0}"' -f $Family.Id)
    }) + @(
        ('HKR,DEFAULT,Mode,,"{0}"' -f $DefaultMode)
        'HKR,DEFAULT,drv,,v9xdisp.drv'
        'HKR,DEFAULT,drv2,,v9xdisp.drv'
        'HKR,DEFAULT,vdd,,"*vdd,*vflatd"'
    ) + @(if ($manualMiniVdd) {
        'HKR,DEFAULT,minivdd,,v9xmini.vxd'
    }) + @(
        'HKR,DEFAULT,RefreshRate,,0'
        'HKR,DEFAULT,PCIRebalance,,1'
        'HKR,DEFAULT,ExtModeSwitch,,0'
        # The 4-bpp fallback hands the mode back to the stock VGA driver.
        'HKR,"MODES\4\640,480",drv,,vga.drv'
        'HKR,"MODES\4\640,480",vdd,,*vdd'
    )

    # Per-chip MODES capability. A single-chip family writes them straight
    # into the shared registry section, exactly as the rewrite did.
    if (-not $perChipRegistry) {
        $lines += Get-V9xInfModeLines -Modes $chips[0].Modes
    }

    $lines += @(
        ('HKCR,CLSID\{0},,,"Velocity9x Settings Page"' -f $script:V9xSettingsPageClsid)
        ('HKCR,CLSID\{0}\InProcServer32,,,"v9xsetp.dll"' -f $script:V9xSettingsPageClsid)
        ('HKCR,CLSID\{0}\InProcServer32,ThreadingModel,,"Apartment"' -f
         $script:V9xSettingsPageClsid)
        # Both key paths are quoted because both contain a space, and Win95's
        # SetupX will not parse an unquoted AddReg key path that does. It fails
        # silently: on the 486 every other line of this section applied - the
        # HKCR ones next to it included, which have no spaces - while these two
        # created nothing at all, so the Velocity9x tab never appeared and there
        # was no error to say why. Win98's SetupX is more forgiving, which is
        # why this survived until a Win95 machine met it. Windows' own
        # MSDISP.INF quotes the same key for the same reason.
        ('HKLM,"Software\Microsoft\Windows\CurrentVersion\Controls Folder\Display' +
         '\shellex\PropertySheetHandlers\Velocity9x",,,"{0}"' -f $script:V9xSettingsPageClsid)
        ('HKLM,"Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved",' +
         '{0},,"Velocity9x Settings Page"' -f $script:V9xSettingsPageClsid)
        # The lines above are necessary but not sufficient. Windows 98 validates
        # a Display property-sheet handler against a Tag DWORD derived from a
        # per-machine seed, ignores the handler when it does not check out, and
        # deletes the key - so an INF, which cannot know the seed, can never
        # register this page on its own. V9xRegisterPage recovers the seed from
        # a handler Windows has already accepted and writes the Tag. RunOnce
        # fires it at the first boot after the install, which is also the boot
        # where the shell first reads the handler list.
        ('HKLM,Software\Microsoft\Windows\CurrentVersion\RunOnce,V9xSettingsPage,,' +
         '"rundll32.exe v9xsetp.dll,V9xRegisterPage"')
    ) + @(if ($Family.Build.MiniVddVbeCollect -ne $false) {
        # Run, not RunOnce, and the two must not be mistaken for each other:
        # V9xRegisterPage writes the property-sheet Tag once, at the first
        # boot after the install, while the mode synchronizer must re-run
        # every boot - the inventory changes whenever the card, the panel or
        # the BIOS-visible mode set changes - and is idempotent when nothing
        # did. Only scan-enabled families receive this entry: a disabled
        # family's mode list is its INF, already in the registry.
        ('HKLM,Software\Microsoft\Windows\CurrentVersion\Run,V9xSyncModes,,' +
         '"rundll32.exe v9xsetp.dll,V9xSyncModes"')
    })

    if ($perChipRegistry) {
        foreach ($chip in $chips) {
            $lines += @(
                ''
                ('[Velocity9x.Registry.{0}]' -f $chip.Id)
            )
            # Where the mini-VDD lands when the manual model must not have one.
            # A PCI model reaches this section and gets it; the manual model
            # never AddRegs any per-chip section, so it does not.
            if (-not $manualMiniVdd) {
                $lines += 'HKR,DEFAULT,minivdd,,v9xmini.vxd'
            }
            $lines += Get-V9xInfModeLines -Modes $chip.Modes
        }
    }

    # The manual model's own MODES list: derived, not declared. It is the
    # intersection of every chip's modes narrowed to what fits the VRAM the
    # manifest says this card has, so it never offers a mode Enable would go on
    # to refuse.
    if ($manual) {
        $lines += @(
            ''
            '[Velocity9x.Registry.Manual]'
        )
        $lines += Get-V9xInfModeLines -Modes $manualModes
        # The standard VGA resource map, copied from the VGA.LogConfig every
        # model in MSDISP.INF shares: the two register windows, the A0000 and
        # B8000 apertures, and the alternatives the option ROM may occupy. The
        # linear framebuffer is deliberately absent, exactly as it is there -
        # S3 models with linear apertures use this same section, and on this
        # card the aperture sits at 0x7F000000, far above anything the
        # Configuration Manager arbitrates.
        $lines += @(
            ''
            '[Velocity9x.LogConfig]'
            'ConfigPriority=HARDWIRED'
            'IOConfig=3B0-3BB'
            'IOConfig=3C0-3DF'
            'MemConfig=A0000-AFFFF'
            'MemConfig=B8000-BFFFF'
            'MemConfig=C0000-C7FFF,D0000-D7FFF,E0000-E5FFF,E0000-E7FFF'
        )
    }

    $lines += @(
        ''
        '[Strings]'
        ('Provider="{0}"' -f $inf.Provider)
        ('Manufacturer="{0}"' -f $inf.Manufacturer)
        # One entry per chip. The models section inlines its own description, so
        # these are not what SetupX reads - but a single DeviceDesc in a
        # multi-chip family names one card and silently implies the others are
        # not there.
    ) + @($chips | ForEach-Object {
        'DeviceDesc.{0}="{1}"' -f $_.Id, $_.DeviceDesc
    }) + @(if ($manual) {
        'DeviceDesc.manual="{0}"' -f $manual.Description
    }) + @(
        ('DiskName="{0}"' -f $inf.DiskName)
    )
    $lines
}

# Assertions the old build-active-package.ps1 ran against the rewritten text.
# The single-hardware-ID check becomes set equality against the manifest, which
# is what lets a family carry more than one chip.
function Assert-V9xInf {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string[]]$Lines,
        [Parameter(Mandatory = $true)]$Family,
        [Parameter(Mandatory = $true)][string]$DefaultMode
    )

    $text = $Lines -join "`r`n"
    if ($text -match '%[A-Za-z][A-Za-z0-9_]*%') {
        throw "The generated INF contains an unresolved string token."
    }

    $actualIds = @([regex]::Matches(
        $text, 'PCI\\VEN_[0-9A-Fa-f]{4}&DEV_[0-9A-Fa-f]{4}') |
        ForEach-Object { $_.Value.ToUpperInvariant() } | Sort-Object -Unique)
    $expectedIds = @(Get-V9xFamilyHardwareIds -Family $Family)
    $difference = @(Compare-Object -ReferenceObject $expectedIds `
        -DifferenceObject $actualIds)
    if ($difference.Count -ne 0) {
        throw ("The generated INF hardware-ID set does not match family " +
               "$($Family.Id). Expected: $($expectedIds -join ', '); " +
               "found: $($actualIds -join ', ').")
    }

    # MODES\24 and MODES\32 were forbidden here while the driver had no 24- or
    # 32-bpp support and an INF offering them would have advertised a mode
    # Enable then refused. They are generated from the manifest now, and the
    # per-chip required-entry check below is what keeps the INF and the
    # manifest agreeing about which depths exist.
    foreach ($forbidden in @('DDC', 'carddvdd')) {
        if ($text.IndexOf($forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "The generated INF contains out-of-scope entry $forbidden."
        }
    }

    # The synchronizer entry and its devnode marker are asserted present for
    # scan-enabled families and asserted absent otherwise, so a disabled
    # family can neither gain a per-boot process nor lose the pair silently.
    if ($Family.Build.MiniVddVbeCollect -ne $false) {
        foreach ($syncEntry in @(
                'Run,V9xSyncModes,,"rundll32.exe v9xsetp.dll,V9xSyncModes"',
                ('V9xFamily,,"{0}"' -f $Family.Id))) {
            if ($text.IndexOf($syncEntry, [StringComparison]::OrdinalIgnoreCase) -lt 0) {
                throw "The generated INF is missing required entry $syncEntry."
            }
        }
    } elseif ($text -match 'V9xSyncModes|V9xFamily') {
        throw ("The generated INF carries the mode synchronizer for family " +
               "$($Family.Id), which assembles VBE collection out.")
    }

    $required = @('v9xdisp.drv', 'v9xmini.vxd', 'v9xhal.dll', 'v9xsetp.dll',
                  'Controls Folder\Display\shellex\PropertySheetHandlers\Velocity9x',
                  'RunOnce,V9xSettingsPage,,"rundll32.exe v9xsetp.dll,V9xRegisterPage"',
                  "CLSID\$script:V9xSettingsPageClsid\InProcServer32",
                  "DEFAULT,Mode,,`"$DefaultMode`"",
                  'DEFAULT,vdd,,"*vdd,*vflatd"',
                  'DEFAULT,RefreshRate,,0',
                  'DEFAULT,PCIRebalance,,1')
    foreach ($chip in @($Family.Chips)) {
        foreach ($mode in @($chip.Modes)) {
            $required += 'MODES\{0}\{1},{2}' -f $mode.BitsPerPixel, $mode.Width,
                $mode.Height
        }
    }
    foreach ($entry in ($required | Sort-Object -Unique)) {
        if ($text.IndexOf($entry, [StringComparison]::OrdinalIgnoreCase) -lt 0) {
            throw "The generated INF is missing required entry $entry."
        }
    }
    if ($text -match '(?im)^HKR,CURRENT,') {
        throw "The generated INF must let Windows create the volatile CURRENT display key."
    }

    # The hardware-ID set equality above passes a no-ID model line by
    # construction: it contributes no PCI\VEN_ match. What follows is the
    # positive half - exactly one ID-less slot, only where declared, and its
    # MODES list exactly the derived one.
    $modelsSection = $Family.Inf.ModelsSection
    if (-not $modelsSection) { $modelsSection = 'Velocity9x.Models' }
    $manual = $null
    $manualLine = $null
    if ($Family.Inf -is [hashtable] -and $Family.Inf.ContainsKey('ManualSelect')) {
        $manual = $Family.Inf.ManualSelect
        $manualLine = Get-V9xInfManualModelLine -Family $Family
    }

    $idLess = 0
    foreach ($line in (Get-V9xInfSectionBody -Lines $Lines -Section $modelsSection)) {
        if ($line -match ('^"[^"]+"=\S+,PCI\\VEN_[0-9A-Fa-f]{4}&DEV_[0-9A-Fa-f]{4}' +
                          '(&SUBSYS_[0-9A-Fa-f]{8},PCI\\VEN_[0-9A-Fa-f]{4}&DEV_[0-9A-Fa-f]{4})?$')) {
            continue
        }
        if ($manualLine -and $line -eq $manualLine) {
            $idLess++
            continue
        }
        throw ("The generated INF models section carries a line that is neither " +
               "a PCI model nor family $($Family.Id)'s declared manual-select " +
               "model: '$line'.")
    }
    if (-not $manual) {
        foreach ($absent in @('Velocity9x.Install.Manual', 'Velocity9x.LogConfig')) {
            if ($text -match [regex]::Escape($absent)) {
                throw ("The generated INF names $absent but family " +
                       "$($Family.Id) declares no Inf.ManualSelect.")
            }
        }
        return
    }
    if ($idLess -ne 1) {
        throw ("The generated INF has $idLess manual-select model line(s); " +
               "family $($Family.Id) declares exactly one.")
    }
    # A CompatibleId belongs in the third field with the second left empty. In
    # the hardware-id field the same string would rank level with an exact
    # match, which is the thing this model must never do - it is chosen by hand,
    # and the chip check that can actually refuse a stranger's card lives in
    # identify_without_pci, not here.
    if ($manual.CompatibleId -and
        $manualLine -notmatch '^"[^"]+"=\S+,,\s\S+$') {
        throw ("The generated INF's manual-select model must carry its " +
               "CompatibleId in the compatible-id field with the hardware-id " +
               "field empty; got '$manualLine'.")
    }

    $manualModes = @(Get-V9xFamilyManualSelectModes -Family $Family)
    $expectedModes = @($manualModes | ForEach-Object {
            'MODES\{0}\{1},{2}' -f $_.BitsPerPixel, $_.Width, $_.Height
        } | Sort-Object -Unique)
    $manualBody = @(Get-V9xInfSectionBody -Lines $Lines `
        -Section 'Velocity9x.Registry.Manual')
    # [regex]::Match rather than -match: $Matches set inside a ForEach-Object
    # block is not what the block reads back, so the -match form silently
    # returned this function's previous match ten times over.
    $actualModes = @(@($manualBody | ForEach-Object {
            $found = [regex]::Match($_, 'MODES\\[0-9]+\\[0-9]+,[0-9]+')
            if ($found.Success) { $found.Value }
        }) | Sort-Object -Unique)
    $difference = @(Compare-Object -ReferenceObject $expectedModes `
        -DifferenceObject $actualModes)
    if ($difference.Count -ne 0) {
        throw ("The generated INF's manual-select MODES list does not match the " +
               "modes derived for family $($Family.Id). Expected: " +
               "$($expectedModes -join ', '); found: $($actualModes -join ', ').")
    }
    if ($manualBody.Count -ne $actualModes.Count) {
        throw ("The generated INF's [Velocity9x.Registry.Manual] carries " +
               "$($manualBody.Count) line(s) for $($actualModes.Count) modes; " +
               "it may contain nothing else.")
    }
    $manualInstall = @(Get-V9xInfSectionBody -Lines $Lines `
        -Section 'Velocity9x.Install.Manual')
    if ('AddReg=Velocity9x.Registry,Velocity9x.Registry.Manual' -notin $manualInstall) {
        throw ("The generated INF's [Velocity9x.Install.Manual] must AddReg both " +
               "Velocity9x.Registry and Velocity9x.Registry.Manual; it carries: " +
               "$($manualInstall -join ' / ').")
    }
    # Without this the devnode gets no resources and Device Manager reports the
    # device as not present, which is a working driver nobody can reach.
    if ('LogConfig=Velocity9x.LogConfig' -notin $manualInstall) {
        throw ("The generated INF's [Velocity9x.Install.Manual] must declare " +
               "LogConfig=Velocity9x.LogConfig; a model with no hardware ID " +
               "has no bus to report its resources. It carries: " +
               "$($manualInstall -join ' / ').")
    }
    # When the manual model must have no mini-VDD, the value has to be absent
    # from both sections it AddRegs, and present in every per-chip section so
    # the PCI models keep theirs. Checked positively in both directions: a
    # mini-VDD leaking back into the shared section is the exact regression that
    # put a Win95 devnode into Code 24.
    if ($manual.ContainsKey('MiniVdd') -and $manual.MiniVdd -eq $false) {
        foreach ($section in @('Velocity9x.Registry', 'Velocity9x.Registry.Manual')) {
            $body = @(Get-V9xInfSectionBody -Lines $Lines -Section $section)
            if (@($body | Where-Object { $_ -match 'DEFAULT,minivdd' }).Count -ne 0) {
                throw ("Family $($Family.Id) declares ManualSelect.MiniVdd " +
                       "false, so [$section] must not set DEFAULT,minivdd - the " +
                       "manual model AddRegs it and would get a mini-VDD.")
            }
        }
        foreach ($chip in @($Family.Chips)) {
            $body = @(Get-V9xInfSectionBody -Lines $Lines `
                -Section ('Velocity9x.Registry.{0}' -f $chip.Id))
            if ('HKR,DEFAULT,minivdd,,v9xmini.vxd' -notin $body) {
                throw ("Family $($Family.Id) moved DEFAULT,minivdd out of the " +
                       "shared section, so [Velocity9x.Registry.$($chip.Id)] " +
                       "must set it; otherwise that chip's own model loses it too.")
            }
        }
    } elseif (@(Get-V9xInfSectionBody -Lines $Lines -Section 'Velocity9x.Registry' |
                Where-Object { $_ -match 'DEFAULT,minivdd' }).Count -eq 0) {
        throw ("The generated INF's [Velocity9x.Registry] must set " +
               "DEFAULT,minivdd unless the family declares " +
               "ManualSelect.MiniVdd = `$false.")
    }

    $logConfig = @(Get-V9xInfSectionBody -Lines $Lines -Section 'Velocity9x.LogConfig')
    if ($logConfig -notcontains 'ConfigPriority=HARDWIRED' -or
        @($logConfig | Where-Object { $_ -like 'IOConfig=*' }).Count -eq 0 -or
        @($logConfig | Where-Object { $_ -like 'MemConfig=*' }).Count -eq 0) {
        throw ("The generated INF's [Velocity9x.LogConfig] must declare " +
               "ConfigPriority plus at least one IOConfig and one MemConfig; " +
               "it carries: $($logConfig -join ' / ').")
    }
    # DEFAULT,Mode is written by the shared registry section the manual model
    # also AddRegs, so it has to be a mode the manual MODES list advertises.
    # Test-V9xFamilyManifest checks the manifest's own DefaultMode; this catches
    # a -ForceModeIndex override, which the manifest cannot see.
    if ($DefaultMode -notin @($manualModes |
            ForEach-Object { '{0},{1},{2}' -f $_.BitsPerPixel, $_.Width, $_.Height })) {
        throw ("The generated INF defaults to mode '$DefaultMode', which family " +
               "$($Family.Id)'s manual-select model does not advertise.")
    }
}
