# Assemble a transfer folder small enough for one 1.44 MB floppy.
#
# Both chip packages together are well under the limit, so the floppy carries
# both and the target machine picks the matching folder. That matters for an
# offline install: the card cannot be identified from here, and a second trip
# with the other package is expensive when the machine has no network.
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
foreach ($family in $floppyFamilies) {
    Copy-Item -LiteralPath $sources[$family.Id] `
        -Destination (Join-Path $outputDir $family.Floppy.Folder) -Recurse -Force
}

# Recovery instructions belong at the root as well as inside each package: if
# the machine will not display after the install, the reader needs them without
# having to remember which folder was used.
Copy-Item -LiteralPath (Join-Path $sources[$floppyFamilies[0].Id] "RECOVER.TXT") `
    -Destination (Join-Path $outputDir "RECOVER.TXT") -Force

# The chip table and the hardware-ID sentence are generated so that adding a
# family updates the disk's own instructions. Column widths and the 73-column
# wrap match the hand-written body around them.
$chipTable = @()
$hardwareIdWords = @()
foreach ($family in $floppyFamilies) {
    foreach ($chip in @($family.Chips)) {
        $chipTable += "   {0}{1}{2}" -f $family.Floppy.Folder.PadRight(9),
            $chip.Name.PadRight(23), $family.Floppy.HardwareIdHint
        $hardwareIdWords += "VEN_{0}&DEV_{1}" -f $chip.VendorId, $chip.DeviceId
    }
}
$chipTableText = $chipTable -join "`n"

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

$hardwareIdSentence = Format-V9xParagraph -Text (
    "and read the hardware ID. It will contain " +
    ($hardwareIdWords -join " or ") +
    ". If it shows neither, this driver does not support your card and the " +
    "install will refuse to match it.")

$readme = @"
VELOCITY9X $ProductVersion - WINDOWS 98 DISPLAY DRIVER
Build: $BuildId

READ THIS BEFORE INSTALLING.

This is an engineering bring-up driver, not a release driver. A failed
install leaves Windows 98 unable to display. Do not install it on a machine
you cannot recover by hand.


1. WHICH FOLDER DO I USE?

This disk carries both supported chips. Use exactly one.

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

Copy the folder for your chip to the hard disk first - installing from a
floppy works, but Windows may ask for the disk again later.

   1. Control Panel, System, Device Manager.
   2. Expand Display adapters and open your S3 adapter.
   3. Driver, Update Driver, choose a specific driver or location.
   4. Have Disk, and browse to the copied folder.
   5. Select the Velocity9x entry for your chip.
   6. Let Windows copy the files. Do not accept a different device ID.
   7. Shut down fully when prompted. Do not warm-restart the first boot.

The first boot comes up at 640x480 in 256 colours.

Full detail is in INSTALL.TXT inside the folder you used. FIRSTBOOT.TXT
there is the step-by-step checklist for the first boot.


4. CHECKING IT WORKED

Run these from the copied folder after the desktop appears:

   V9XGDI.EXE /auto      framebuffer drawing and pixel readback test
   V9XMSW.EXE /set:800x600x16    switch mode and verify
   V9XMSW.EXE /depth:10  alternate 8 and 16 bpp ten times
   V9XPAL.EXE            palette test, run this in a 256-colour mode
   V9XSET.EXE            read-only status panel

A "Velocity9x" tab also appears in Display Properties showing the detected
adapter, video memory, active mode and which acceleration paths are live.

Supported modes: 640x400 at 256 colours; 640x480, 800x600 and 1024x768 at
256 colours and High Color (16 bit). On the S3 cards, also True Color (32 bit)
at those three sizes and 1280x1024 at 256 colours and High Color; a 2 MB card
declines the largest of those for want of memory. There are no 24-bit modes
anywhere, and nothing above 1280x1024. That is expected, not a fault.


5. IF IT DOES NOT BOOT TO A DESKTOP

Stop after ONE attempt. Do not reboot repeatedly. Read RECOVER.TXT on this
disk. In short: power off, press F8 before Windows starts, choose Safe Mode,
then Device Manager and remove the Velocity9x display adapter, reboot and
let Windows redetect Standard PCI Graphics Adapter (VGA).


6. REPORTING A PROBLEM

Copy these files off the machine if they exist:

   C:\V9XBOOT.INI    how far the driver got during startup
   C:\V9XHW.INI      what the driver detected about the card
   C:\V9XGDI.INI     last framebuffer test result
   C:\V9XMSW.INI     last mode-switch test result
   C:\V9XDD.INI      DirectDraw and Direct3D probe results
   C:\V9XTRACE.INI   DirectDraw callback trace after a fault

Also note the chip, the mode in use when it went wrong, and the build ID at
the top of this file. V9XSET.EXE has a Copy report button that puts most of
this on the clipboard in one go.
"@

$readmePath = Join-Path $outputDir "README.TXT"
Set-Content -LiteralPath $readmePath -Encoding Ascii `
    -Value ($readme -replace "`r`n", "`n" -replace "`n", "`r`n")

$files = @(Get-ChildItem -LiteralPath $outputDir -Recurse -File)
$total = ($files | Measure-Object -Property Length -Sum).Sum
if ($total -gt $floppyCapacity) {
    throw ("The floppy folder is {0:N0} bytes, over the {1:N0} usable on a " +
           "1.44 MB floppy." -f $total, $floppyCapacity)
}

Write-Output "Velocity9x $ProductVersion floppy transfer folder: $outputDir"
Write-Output ("  {0} files, {1:N0} bytes ({2:N0} bytes free on a 1.44 MB floppy)" -f
    $files.Count, $total, ($floppyCapacity - $total))

if ($Zip) {
    $zipPath = Join-Path $repoRoot (
        "build\velocity9x-$ProductVersion-$BuildId.zip")
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($outputDir, $zipPath)
    Write-Output ("Archive: {0} ({1:N0} bytes)" -f $zipPath,
        (Get-Item -LiteralPath $zipPath).Length)
}
