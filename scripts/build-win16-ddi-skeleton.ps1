[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = "C:\98DDK",
    # Family manifest id under packaging\families.
    [string]$Family = 's3',
    # Build-time variant declared by the family manifest (Matrox 8bpp/16bpp).
    [string]$Variant,
    [ValidateRange(-1, 5)]
    [int]$ForceModeIndex = -1,
    [switch]$BootTrace,
    # Write a DosBox= step to V9XBOOT.INI, flushed, at each point of the
    # DOS-box round trip. The instrument for
    # docs\issues\2026-08-28-dos-box-entry-hang-gma950.md, where the mini-VDD
    # cannot be traced and the display driver can.
    [switch]$DosBoxTrace
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "lib\family.ps1")

$familyManifest = Import-V9xFamily -RepoRoot $repoRoot -Id $Family
$variants = @($familyManifest.Build.Variants | Where-Object { $_ })
$activeVariant = $null
if ($variants.Count -ne 0) {
    if (-not $Variant) {
        $default = @($variants | Where-Object { $_.Default })
        if ($default.Count -ne 1) {
            throw "Family $Family must declare exactly one default variant."
        }
        $Variant = $default[0].Id
    }
    $activeVariant = @($variants | Where-Object { $_.Id -eq $Variant })
    if ($activeVariant.Count -ne 1) {
        throw ("Family $Family has no variant '$Variant'. Known: " +
               (@($variants | ForEach-Object { $_.Id }) -join ', '))
    }
    $activeVariant = $activeVariant[0]
    if ($ForceModeIndex -ge 0 -and
        $ForceModeIndex -notin @($activeVariant.AllowedModeIndexes)) {
        throw ("Family $Family variant $Variant supports mode index(es) " +
               (@($activeVariant.AllowedModeIndexes) -join ', ') +
               "; $ForceModeIndex was requested.")
    }
} elseif ($Variant) {
    throw "Family $Family declares no build variants."
}

$outputDir = Join-Path $repoRoot $familyManifest.Build.SkeletonOutput

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "win16-ddi-local"
}

$watcomRoot = $env:WATCOM
if (-not $watcomRoot -and (Test-Path -LiteralPath "C:\WATCOM")) {
    $watcomRoot = "C:\WATCOM"
}
if (-not $watcomRoot) {
    throw "Open Watcom was not found. Set WATCOM or install it at C:\WATCOM."
}

$compiler = Join-Path $watcomRoot "binnt\wcc.exe"
$assembler = Join-Path $watcomRoot "binnt\wasm.exe"
$linker = Join-Path $watcomRoot "binnt\wlink.exe"
$dumper = Join-Path $watcomRoot "binnt64\wdump.exe"
$disassembler = Join-Path $watcomRoot "binnt64\wdis.exe"
$dibEngineLibrary = Join-Path $DdkRoot "lib\win98\DIBENG.LIB"
$runtimeLibrary = Join-Path $watcomRoot "lib286\win\clibc.lib"
$requiredTools = @($compiler, $assembler, $linker, $dumper, $disassembler,
                   $dibEngineLibrary, $runtimeLibrary)
$missingTools = @($requiredTools | Where-Object {
    -not (Test-Path -LiteralPath $_)
})
if ($missingTools.Count -ne 0) {
    throw "Required Win16 build inputs are missing: $($missingTools -join ', ')"
}

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\win')"

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$includeDir = Join-Path $repoRoot "include"
# Compile and link order both come from the manifest; reordering it changes
# the linked image, so the golden compare has to be re-based if it moves.
$sources = @($familyManifest.Build.Sources)
$familyDefines = @($familyManifest.Build.Defines)
$familyRuntimeDefines = @($familyManifest.Build.RuntimeDefines)
if ($activeVariant) {
    $familyDefines += @($activeVariant.Defines)
    $familyRuntimeDefines += @($activeVariant.RuntimeDefines)
}

foreach ($source in $sources) {
    $sourcePath = Join-Path $repoRoot $source.Path
    $objectPath = Join-Path $outputDir "$($source.Name).obj"
    $arguments = @(
        "-bt=windows", "-mc", "-zu", "-zc", "-zls", "-s", "-zq", "-wx", "-we",
        "-i=$includeDir", "-i=$(Join-Path $repoRoot 'src\display16')",
        "-dV9X_BUILD_ID=`"$BuildId`"",
        "-fo=$objectPath", $sourcePath
    )
    # ddi.c already defaults V9X_FORCE_MODE_INDEX to -1. Passing a negative
    # value through wcc's -d option is parsed as another command-line input by
    # this Open Watcom snapshot (E1139), so only define an actual forced mode.
    if ($ForceModeIndex -ge 0) {
        $arguments = @("-dV9X_FORCE_MODE_INDEX=$ForceModeIndex") + $arguments
    }
    if ($BootTrace) {
        $arguments = @("-dV9X_BOOT_TRACE=1") + $arguments
    }
    if ($DosBoxTrace) {
        $arguments = @("-dV9X_DOSBOX_TRACE=1") + $arguments
    }
    foreach ($define in $familyDefines) {
        $arguments = @("-d$define") + $arguments
    }
    & $compiler @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom 16-bit compilation failed for $($source.Path)."
    }
}

$thunkSource = Join-Path $repoRoot "src\display16\dib_thunks.asm"
$thunkObject = Join-Path $outputDir "dib_thunks.obj"
& $assembler "-zq" "-fo=$thunkObject" $thunkSource
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to assemble the DIB Engine forwarding thunks."
}

$runtimeSource = Join-Path $repoRoot "src\display16\runtime.asm"
$runtimeObject = Join-Path $outputDir "runtime.obj"
# -i= is where wasm looks for V9XMAPI.INC, the mini-VDD API contract shared
# with src\minivdd32\loader.asm.
$runtimeAssemblerArguments = @(
    "-zq", "-i=$(Join-Path $repoRoot 'include\asm')", "-fo=$runtimeObject")
foreach ($define in $familyRuntimeDefines) {
    $runtimeAssemblerArguments += "-d$define"
}
$runtimeAssemblerArguments += $runtimeSource
& $assembler @runtimeAssemblerArguments
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to assemble the Win16 framebuffer runtime."
}

$driverPath = Join-Path $outputDir "v9xdisp.drv"
$mapPath = Join-Path $outputDir "v9xdisp.map"
$linkFile = Join-Path $outputDir "v9xdisp.lnk"
$objectNames = @($sources.Name) + @("dib_thunks", "runtime")
$linkLines = @(
    "system windows dll initglobal",
    "name '$driverPath'",
    "option start=DriverInit_",
    "option modname=DISPLAY",
    "option map='$mapPath'",
    "option caseexact",
    "option oneautodata",
    "option heapsize=1024",
    "segment type data preload fixed",
    "segment '_TEXT' preload fixed shared"
)
$linkLines += $objectNames | ForEach-Object {
    "file '$(Join-Path $outputDir "$_.obj")'"
}
$linkLines += @(
    "libfile '$dibEngineLibrary'",
    "library '$runtimeLibrary'",
    "reference RESETHIRESMODE",
    # Ordinal 1 is a C function now (src\display16\gdi_accel.c), not the
    # assembly forwarding thunk it used to be. This driver compiles PASCAL
    # exports with their names uppercased, so the ordinal has to name the
    # mangled symbol - the same =CONTROL / =ENABLE / =DISABLE pattern the rest
    # of this list already uses for its C exports.
    "export BitBlt.1=BITBLT",
    "export ColorInfo.2", "export Control.3=CONTROL",
    "export Disable.4=DISABLE", "export Enable.5=ENABLE", "export EnumDFonts.6",
    "export EnumObj.7", "export Output.8", "export Pixel.9",
    "export RealizeObject.10", "export StrBlt.11", "export ScanLR.12",
    "export DeviceMode.13", "export ExtTextOut.14",
    "export GetCharWidth.15", "export DeviceBitmap.16",
    "export FastBorder.17", "export SetAttribute.18",
    "export DibBlt.19", "export CreateDIBitmap.20",
    "export DibToDevice.21", "export SetPalette.22=SETPALETTE",
    "export GetPalette.23", "export SetPaletteTranslate.24",
    "export GetPaletteTranslate.25", "export UpdateColors.26",
    "export StretchBlt.27", "export StretchDIBits.28",
    "export SelectBitmap.29", "export BitmapBits.30",
    "export ReEnable.31=REENABLE", "export Inquire.101",
    "export SetCursor.102", "export MoveCursor.103",
    "export CheckCursor.104",
    # Ordinal 500 is where every Windows 98 DDK display sample exports this
    # (98DDK\src\display\mini\{framebuf,mini,s3v,xga}\*.DEF). USER calls it to
    # say whether a repaint request may be issued now, and the enabling call is
    # the driver's only hook that is reliably after USER has finished a display
    # change - which is what a live mode switch needs to repaint the desktop.
    "export UserRepaintDisable.500=USERREPAINTDISABLE",
    "export ValidateMode.700=VALIDATEMODE"
)
Set-Content -LiteralPath $linkFile -Value $linkLines -Encoding Ascii

& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link the Win16 DDI skeleton."
}

# All post-link auditing lives in audit-family-binary.ps1: the chip-agnostic
# checks stay script logic there, and the chip signature checks are driven by
# the family manifests so a family binary may legitimately carry more than one
# chip. See docs\plans\multi-chip-restructure.md.
& (Join-Path $PSScriptRoot "audit-family-binary.ps1") `
    -Family $Family -OutputDir $outputDir -BuildId $BuildId `
    -BootTrace:$BootTrace | Write-Verbose

$variantDescription = if ($activeVariant) { " $($activeVariant.Id)" } else { "" }
Write-Output ("Built {0}{1} Win16 DDI candidate: {2}" -f
    $familyManifest.DisplayName, $variantDescription, $driverPath)
