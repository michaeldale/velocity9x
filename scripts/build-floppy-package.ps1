# Assemble one 1.44 MB floppy per family that opts in (Floppy.Include).
#
# The families used to share one disk, so an offline machine could pick the
# matching folder without a second trip. With the OpenGL ICD in every package
# they no longer fit together (docs\issues\2026-09-26-floppy-over-capacity-
# with-opengl-icd.md), and each family now gets its own disk under
# build\floppy\<Folder>.
#
# The output is a plain directory tree. Nothing is archived, because Windows 98
# has no built-in extractor and an offline machine may have no unzip tool at
# all - the files must be usable straight off the disk. Use -Zip only for
# transfer over a network to a machine that can unpack it.
[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = "C:\98DDK",
    [switch]$SkipBuild,
    [switch]$Zip
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "lib\family.ps1")
$ProductVersion = Get-V9xProductVersion -RepoRoot $repoRoot
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "floppy-local"
}

# Usable space on a formatted 1.44 MB floppy, after the FAT12 overhead.
$floppyCapacity = 1457664

# Which families ride the disk, in which folder, and in what order on the
# printed chip table is all manifest data.
$floppyFamilies = @(Get-V9xFamilies -RepoRoot $repoRoot |
    Where-Object { $_.Floppy.Include } |
    Sort-Object { [int]$_.Floppy.Order })
if ($floppyFamilies.Count -eq 0) {
    throw "No family manifest opts into the floppy package."
}

if (-not $SkipBuild) {
    foreach ($family in $floppyFamilies) {
        & (Join-Path $PSScriptRoot "build-active-package.ps1") `
            -BuildId $BuildId -DdkRoot $DdkRoot -Family $family.Id
    }
}

$sources = @{}
foreach ($family in $floppyFamilies) {
    $source = Join-Path $repoRoot ("build\{0}" -f
        (Split-Path -Leaf $family.Build.PackageOutput))
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Missing package $source. Run without -SkipBuild."
    }
    $sources[$family.Id] = $source
}

$outputDir = Join-Path $repoRoot "build\floppy"
if (Test-Path -LiteralPath $outputDir) {
    Remove-Item -LiteralPath $outputDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

function Format-V9xParagraph {
    param([string]$Text, [int]$Width = 73)
    $lines = @()
    $current = ""
    foreach ($word in ($Text -split '\s+')) {
        if (-not $current) {
            $current = $word
        } elseif (($current.Length + 1 + $word.Length) -le $Width) {
            $current = "$current $word"
        } else {
            $lines += $current
            $current = $word
        }
    }
    if ($current) { $lines += $current }
    $lines -join "`n"
}

# One disk per family (docs\issues\2026-09-26-floppy-over-capacity-with-
# opengl-icd.md): with the OpenGL ICD in every package, the families no
# longer fit one disk together. Each disk is build\floppy\<Folder>, holding
# README.TXT and RECOVER.TXT at its root and the family's package in the
# folder of the same name, so the Have Disk steps read as they always did.
$disks = @()
foreach ($family in $floppyFamilies) {
    $diskDir = Join-Path $outputDir $family.Floppy.Folder
    New-Item -ItemType Directory -Force -Path $diskDir | Out-Null
    Copy-Item -LiteralPath $sources[$family.Id] `
        -Destination (Join-Path $diskDir $family.Floppy.Folder) -Recurse -Force

    # Recovery instructions belong at the root as well as inside the package:
    # if the machine will not display after the install, the reader needs them
    # without opening a folder.
    Copy-Item -LiteralPath (Join-Path $sources[$family.Id] "RECOVER.TXT") `
        -Destination (Join-Path $diskDir "RECOVER.TXT") -Force

    # The chip table and the hardware-ID sentence come from the manifest, so
    # a chip added to the family updates the disk's own instructions.
    $chipTable = @()
    $hardwareIdWords = @()
    foreach ($chip in @($family.Chips)) {
        $chipTable += "   {0}{1}{2}" -f $family.Floppy.Folder.PadRight(9),
            $chip.Name.PadRight(23), $family.Floppy.HardwareIdHint
        $hardwareIdWords += "VEN_{0}&DEV_{1}" -f $chip.VendorId, $chip.DeviceId
    }
    $chipTableText = $chipTable -join "`n"

    # A family with a manual-select model can be installed on a card that
    # shows no PCI hardware ID at all, so "none of these means no" is only
    # true on a disk without one.
    $manualSelect = $family.Inf -is [hashtable] -and
        $family.Inf.ContainsKey('ManualSelect')
    $noMatchSentence = if (-not $manualSelect) {
        "If it shows none of those, this disk does not support your card and " +
        "the install will refuse to match it. Another Velocity9x disk may."
    } else {
        "If it shows none of those - a VESA Local Bus card in a machine with " +
        "no PCI bus has no PCI hardware ID to show - then only the " +
        "manual-select entry in " + $family.Floppy.Folder + " can install, " +
        "and only if that really is the chip you have."
    }
    $hardwareIdSentence = Format-V9xParagraph -Text (
        "and read the hardware ID. It will contain " +
        ($hardwareIdWords -join " or ") + ". " + $noMatchSentence)
    $familyName = $family.DisplayName
    $folderName = $family.Floppy.Folder

    $readme = @"
VELOCITY9X $ProductVersion - WINDOWS 98 DISPLAY DRIVER
Build: $BuildId
Disk: $familyName (folder $folderName)

READ THIS BEFORE INSTALLING.

This is an engineering bring-up driver, not a release driver. A failed
install leaves Windows 98 unable to display. Do not install it on a machine
you cannot recover by hand.


1. IS THIS THE RIGHT DISK?

Each Velocity9x disk carries one family. This one is $($familyName):

$chipTableText

To check which card is fitted, in Windows 98 open:

   Control Panel, System, Device Manager,
   Display adapters, <your adapter>, Properties, Details

$hardwareIdSentence


2. BEFORE YOU START

- Confirm the machine boots and displays using the Standard PCI Graphics
  Adapter (VGA) driver. That is your fallback.
- Know how to reach Safe Mode: hold or tap F8 before Windows starts.
- Back up the machine if you can. Restoring a backup is the only recovery
  that restores all registry and device state.
- Serial logging is optional and mostly useful under emulation. On real
  hardware it needs a null-modem cable to a second PC listening on COM1.


3. INSTALL

Copy the $folderName folder to the hard disk first - installing from a
floppy works, but Windows may ask for the disk again later.

   1. Control Panel, System, Device Manager.
   2. Expand Display adapters and open your display adapter.
   3. Driver, Update Driver, choose a specific driver or location.
   4. Have Disk, and browse to the copied folder.
   5. Select the Velocity9x entry for your chip.
   6. Let Windows copy the files. Do not accept a different device ID.
   7. Shut down fully when prompted. Do not warm-restart the first boot.

The first boot comes up at 640x480 in 256 colours. The install also
registers V9XGL.DLL, the OpenGL driver, which OpenGL programs use on a
High Color desktop the card can draw into.

Full detail is in INSTALL.TXT inside the folder. FIRSTBOOT.TXT there is
the step-by-step checklist for the first boot.


4. CHECKING IT WORKED

Run these from the copied folder after the desktop appears:

   V9XGDI.EXE /auto      framebuffer drawing and pixel readback test
   V9XMSW.EXE /set:800x600x16    switch mode and verify
   V9XMSW.EXE /depth:10  alternate 8 and 16 bpp ten times
   V9XPAL.EXE            palette test, run this in a 256-colour mode
   V9XSET.EXE            read-only status panel

A "Velocity9x" tab also appears in Display Properties showing the detected
adapter, video memory, active mode and which acceleration paths are live.

The modes offered are listed in INSTALL.TXT in the folder. A mode that is
not listed there is not a fault.


5. IF IT DOES NOT BOOT TO A DESKTOP

Stop after ONE attempt. Do not reboot repeatedly. Read RECOVER.TXT on this
disk. In short: power off, press F8 before Windows starts, choose Safe Mode,
then Device Manager and remove the Velocity9x display adapter, reboot and
let Windows redetect Standard PCI Graphics Adapter (VGA).


6. REPORTING A PROBLEM

Copy these files off the machine if they exist:

   C:\V9XDIAG\V9XBOOT.INI    how far the driver got during startup
   C:\V9XDIAG\V9XHW.INI      what the driver detected about the card
   C:\V9XDIAG\V9XGDI.INI     last framebuffer test result
   C:\V9XDIAG\V9XMSW.INI     last mode-switch test result
   C:\V9XDIAG\V9XDD.INI      DirectDraw and Direct3D probe results
   C:\V9XDIAG\V9XTRACE.INI   DirectDraw callback trace after a fault
   C:\V9XDIAG\V9XGL.LOG      OpenGL driver log

Also note the chip, the mode in use when it went wrong, and the build ID at
the top of this file. V9XSET.EXE has a Copy report button that puts most of
this on the clipboard in one go.
"@

    Set-Content -LiteralPath (Join-Path $diskDir "README.TXT") -Encoding Ascii `
        -Value ($readme -replace "`r`n", "`n" -replace "`n", "`r`n")

    $files = @(Get-ChildItem -LiteralPath $diskDir -Recurse -File)
    $total = ($files | Measure-Object -Property Length -Sum).Sum
    if ($total -gt $floppyCapacity) {
        throw ("The {0} floppy is {1:N0} bytes, over the {2:N0} usable on a " +
               "1.44 MB floppy." -f $family.Floppy.Folder, $total,
               $floppyCapacity)
    }
    $disks += [pscustomobject]@{
        Family = $family
        Directory = $diskDir
        Files = $files.Count
        Bytes = $total
    }
}

Write-Output "Velocity9x $ProductVersion floppy disks: $outputDir"
foreach ($disk in $disks) {
    Write-Output ("  {0}: {1} files, {2:N0} bytes ({3:N0} bytes free on a 1.44 MB floppy)" -f
        $disk.Family.Floppy.Folder, $disk.Files, $disk.Bytes,
        ($floppyCapacity - $disk.Bytes))
}

if ($Zip) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    foreach ($disk in $disks) {
        $zipPath = Join-Path $repoRoot ("build\velocity9x-{0}-{1}-{2}-floppy.zip" -f
            $ProductVersion, $BuildId, $disk.Family.Id)
        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }
        [IO.Compression.ZipFile]::CreateFromDirectory($disk.Directory, $zipPath)
        Write-Output ("Archive: {0} ({1:N0} bytes)" -f $zipPath,
            (Get-Item -LiteralPath $zipPath).Length)
    }
}
