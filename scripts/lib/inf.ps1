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

function New-V9xInfText {
    param(
        [Parameter(Mandatory = $true)]$Family,
        [Parameter(Mandatory = $true)][string]$DefaultMode
    )

    $inf = $Family.Inf
    $chips = @($Family.Chips)
    $models = $inf.ModelsSection
    if (-not $models) { $models = 'Velocity9x.Models' }

    # The header used to name one hardcoded adapter, which stopped being true
    # the moment a family carried two chips. It is generated from the manifest
    # now, so it cannot drift from the models below.
    $lines = @(
        '; Velocity9x Windows 98SE bring-up package'
        ('; Family {0}: {1}' -f $Family.Id, $Family.DisplayName)
    ) + @($chips | ForEach-Object {
        '; Supported adapter: {0}, PCI {1}:{2}' -f $_.Name, $_.VendorId, $_.DeviceId
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
        $lines += '"{0}"={1},PCI\VEN_{2}&DEV_{3}' -f $chip.DeviceDesc, $section,
            $chip.VendorId, $chip.DeviceId
    }

    foreach ($chip in $chips) {
        $section = $installSections[$chip.Id]
        $addReg = if ($chips.Count -eq 1) {
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
        ('HKR,DEFAULT,Mode,,"{0}"' -f $DefaultMode)
        'HKR,DEFAULT,drv,,v9xdisp.drv'
        'HKR,DEFAULT,drv2,,v9xdisp.drv'
        'HKR,DEFAULT,vdd,,"*vdd,*vflatd"'
        'HKR,DEFAULT,minivdd,,v9xmini.vxd'
        'HKR,DEFAULT,RefreshRate,,0'
        'HKR,DEFAULT,PCIRebalance,,1'
        'HKR,DEFAULT,ExtModeSwitch,,0'
        # The 4-bpp fallback hands the mode back to the stock VGA driver.
        'HKR,"MODES\4\640,480",drv,,vga.drv'
        'HKR,"MODES\4\640,480",vdd,,*vdd'
    )

    # Per-chip MODES capability. A single-chip family writes them straight
    # into the shared registry section, exactly as the rewrite did.
    if ($chips.Count -eq 1) {
        $lines += Get-V9xInfModeLines -Modes $chips[0].Modes
    }

    $lines += @(
        ('HKCR,CLSID\{0},,,"Velocity9x Settings Page"' -f $script:V9xSettingsPageClsid)
        ('HKCR,CLSID\{0}\InProcServer32,,,"v9xsetp.dll"' -f $script:V9xSettingsPageClsid)
        ('HKCR,CLSID\{0}\InProcServer32,ThreadingModel,,"Apartment"' -f
         $script:V9xSettingsPageClsid)
        ('HKLM,Software\Microsoft\Windows\CurrentVersion\Controls Folder\Display' +
         '\shellex\PropertySheetHandlers\Velocity9x,,,"{0}"' -f $script:V9xSettingsPageClsid)
        ('HKLM,Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved,' +
         '{0},,"Velocity9x Settings Page"' -f $script:V9xSettingsPageClsid)
    )

    if ($chips.Count -gt 1) {
        foreach ($chip in $chips) {
            $lines += @(
                ''
                ('[Velocity9x.Registry.{0}]' -f $chip.Id)
            )
            $lines += Get-V9xInfModeLines -Modes $chip.Modes
        }
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

    foreach ($forbidden in @('MODES\24', 'MODES\32', 'DDC', 'carddvdd')) {
        if ($text.IndexOf($forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "The generated INF contains out-of-scope entry $forbidden."
        }
    }

    $required = @('v9xdisp.drv', 'v9xmini.vxd', 'v9xhal.dll', 'v9xsetp.dll',
                  'Controls Folder\Display\shellex\PropertySheetHandlers\Velocity9x',
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
}
