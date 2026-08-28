[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = "C:\98DDK",
    [switch]$DisableVbeCollect,
    # Assemble in the set-and-ask-again mode sweep. Off by default and
    # deliberately so: it issues 4F02h into the real video BIOS at
    # Device_Init, which is a heavier call than the collection's and can hang
    # a boot on a BIOS that does not come back. See V9xMini_Vbe_Sweep in
    # src\minivdd32\loader.asm and
    # docs\decisions6-08-28-pineview-vbe-mode-list.md for what it is for.
    [switch]$ModeSweep,
    # Differential builds for the full-screen DOS box hang,
    # docs\issues\2026-08-28-dos-box-entry-hang-gma950.md. Experiments, not
    # shipping options: neither is in any package the release folder carries.
    #
    # -VgaReturn hooks PRE_HIRES_TO_VGA and sets INT 10h mode 3 there, before
    # the main VDD takes its own route out of the linear-framebuffer mode. It
    # runs the real video BIOS with no timeout and can hang a machine that did
    # not hang before.
    #
    # -NoDpms removes the unguarded S3 sequencer and CRTC writes from
    # V9xMini_Set_Dpms, which the generic VBE build otherwise issues on
    # whatever silicon it is loaded against.
    [switch]$VgaReturn,
    [switch]$NoDpms,
    # Which family's baseline VBE mode numbers become the generated rescue-probe
    # list. The mini-VDD image itself is family-independent apart from this list
    # and the -DisableVbeCollect gate, which is why the parameter is optional:
    # a standalone build (docs\BUILDING.md) gets the generic VBE family's modes
    # and says so, and build-active-package.ps1 passes the family it is packaging.
    [string]$Family = "vbe"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\minivdd32"

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "minivdd-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "BuildId may contain only letters, digits, dot, underscore, plus, and hyphen."
}

$assembler = Join-Path $DdkRoot "bin\win98\ML.EXE"
$linker = Join-Path $DdkRoot "bin\LINK.EXE"
$ddkInclude = Join-Path $DdkRoot "inc\win98"
$requiredInputs = @($assembler, $linker,
                    (Join-Path $ddkInclude "VMM.INC"),
                    (Join-Path $ddkInclude "MINIVDD.INC"))
$missingInputs = @($requiredInputs | Where-Object {
    -not (Test-Path -LiteralPath $_)
})
if ($missingInputs.Count -ne 0) {
    throw "Required Windows 98 DDK inputs are missing: $($missingInputs -join ', ')"
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$buildInclude = Join-Path $outputDir "V9XBUILD.INC"
$definitionFile = Join-Path $outputDir "v9xmini.def"
$objectPath = Join-Path $outputDir "loader.obj"
$vxdPath = Join-Path $outputDir "v9xmini.vxd"
$mapPath = Join-Path $outputDir "v9xmini.map"

$buildIncludeLines = @(
    "V9xMiniVddBuildId db `"velocity9x:$BuildId`", 0",
    "V9xMiniInitLine db `"V9X-MINI init build=$BuildId`", 13, 10",
    "V9xMiniInitLineLength equ `$ - V9xMiniInitLine",
    "V9xMiniPowerCallbacksLine db `"V9X-MINI power-callbacks-ok callbacks=4 build=$BuildId`", 13, 10",
    "V9xMiniPowerCallbacksLineLength equ `$ - V9xMiniPowerCallbacksLine",
    "V9xMiniDefaultsLine db `"V9X-MINI power-defaults callbacks=0 build=$BuildId`", 13, 10",
    "V9xMiniDefaultsLineLength equ `$ - V9xMiniDefaultsLine",
    "V9xMiniPowerOnLine db `"V9X-MINI monitor-power D0`", 13, 10",
    "V9xMiniPowerOnLineLength equ `$ - V9xMiniPowerOnLine",
    "V9xMiniPowerOffLine db `"V9X-MINI monitor-power low`", 13, 10",
    "V9xMiniPowerOffLineLength equ `$ - V9xMiniPowerOffLine",
    "V9xMiniFailLine db `"V9X-MINI init-fail build=$BuildId`", 13, 10",
    "V9xMiniFailLineLength equ `$ - V9xMiniFailLine",
    "V9xMiniVbeStartLine db `"V9X-MINI vbe-collect start build=$BuildId`", 13, 10",
    "V9xMiniVbeStartLineLength equ `$ - V9xMiniVbeStartLine",
    "V9xMiniVbeDoneLine db `"V9X-MINI vbe-collect done`", 13, 10",
    "V9xMiniVbeDoneLineLength equ `$ - V9xMiniVbeDoneLine",
    "V9xMiniVbeCallLine db `"V9X-MINI vbe-call fn=`"",
    "V9xMiniVbeCallFnHex db `"0000`"",
    "db `" arg=`"",
    "V9xMiniVbeCallArgHex db `"0000`", 13, 10",
    "V9xMiniVbeCallLineLength equ `$ - V9xMiniVbeCallLine",
    "V9xMiniVbeCallRetLine db `"V9X-MINI vbe-call ret=`"",
    "V9xMiniVbeCallRetHex db `"0000`", 13, 10",
    "V9xMiniVbeCallRetLineLength equ `$ - V9xMiniVbeCallRetLine",
    "V9xMiniVbeStatusLine db `"V9X-MINI vbe-status ptr=`"",
    "V9xMiniVbeStatusPtrSegHex db `"0000`"",
    "db `":`"",
    "V9xMiniVbeStatusPtrOffHex db `"0000`"",
    "db `" self=`"",
    "V9xMiniVbeStatusPtrSelfHex db `"0000`"",
    "db `" listed=`"",
    "V9xMiniVbeStatusListedHex db `"0000`"",
    "db `" queried=`"",
    "V9xMiniVbeStatusQueriedHex db `"0000`"",
    "db `" cached=`"",
    "V9xMiniVbeStatusCachedHex db `"0000`"",
    "db `" failed=`"",
    "V9xMiniVbeStatusFailedHex db `"0000`"",
    "db `" overflow=`"",
    "V9xMiniVbeStatusOverflowHex db `"0000`"",
    "db `" probed=`"",
    "V9xMiniVbeStatusProbedHex db `"0000`"",
    "db `" flags=`"",
    "V9xMiniVbeStatusFlagsHex db `"0000`", 13, 10",
    "V9xMiniVbeStatusLineLength equ `$ - V9xMiniVbeStatusLine",
    # The memory-type inspection brackets its MSR reads, so a CPU that claims
    # MTRR support and then faults on RDMSR names the step in a serial capture
    # instead of hanging the boot silently.
    "V9xMiniMtrrReadLine db `"V9X-MINI mtrr-read start`", 13, 10",
    "V9xMiniMtrrReadLineLength equ `$ - V9xMiniMtrrReadLine",
    "V9xMiniMtrrDoneLine db `"V9X-MINI mtrr-read done`", 13, 10",
    "V9xMiniMtrrDoneLineLength equ `$ - V9xMiniMtrrDoneLine"
)
if ($DisableVbeCollect) {
    $buildIncludeLines += @(
        "V9xMiniVbeDisabledLine db `"V9X-MINI vbe-collect disabled build=$BuildId`", 13, 10",
        "V9xMiniVbeDisabledLineLength equ `$ - V9xMiniVbeDisabledLine"
    )
}
if ($NoDpms) {
    $buildIncludeLines += @(
        "V9xMiniDpmsDisabledLine db `"V9X-MINI dpms-disabled build=$BuildId`", 13, 10",
        "V9xMiniDpmsDisabledLineLength equ `$ - V9xMiniDpmsDisabledLine"
    )
}
Set-Content -LiteralPath $buildInclude -Encoding Ascii -Value $buildIncludeLines

# The baseline rescue-probe list.
#
# The static LFB path needs 4F01h data for the mode it is about to set -
# PhysBasePtr above all - and a panel-filtered or otherwise unhelpful BIOS mode
# list must not be able to take that away. So the modes the family actually
# ships are queried by number regardless of what the list walk produced, and
# cached where indexed enumeration cannot reach them: a rescue probe is not a
# discovered mode (see V9X_VBE_RF_ORIGIN_PROBE in include\asm\V9XMAPI.INC).
#
# Generating the list from the manifest is what lets loader.asm stop carrying a
# hand-written seven-mode table that was only ever right for the families whose
# modes happened to be the standard VESA numbers. The bound comes from the
# shared include rather than from a number repeated here.
$probeInclude = Join-Path $outputDir "V9XPROBE.INC"
$asmContract = Join-Path $repoRoot "include\asm\V9XMAPI.INC"
$probeLimit = 0
foreach ($line in (Get-Content -LiteralPath $asmContract)) {
    if ($line -match '^\s*V9X_VBE_BASELINE_PROBE_MAX\s+EQU\s+([0-9]+)\s*$') {
        $probeLimit = [int]$Matches[1]
    }
}
if ($probeLimit -le 0) {
    throw "V9XMAPI.INC does not define V9X_VBE_BASELINE_PROBE_MAX as a count."
}

if ($DisableVbeCollect) {
    # A build with no collection body must not leave a probe list behind for a
    # later build to pick up out of a stale output directory.
    if (Test-Path -LiteralPath $probeInclude) {
        Remove-Item -LiteralPath $probeInclude -Force
    }
    $probeSummary = "no probe list (collection assembled out)"
} else {
    . (Join-Path $PSScriptRoot "lib\family.ps1")
    $probeFamily = Import-V9xFamily -RepoRoot $repoRoot -Id $Family
    $probeModes = [System.Collections.Generic.List[string]]::new()
    foreach ($chip in @($probeFamily.Chips)) {
        foreach ($mode in @($chip.Modes)) {
            if (-not $mode.VbeMode) {
                throw ("Family $Family chip $($chip.Id) has a mode with no " +
                       "VbeMode, so no rescue probe can be generated for it.")
            }
            if ($mode.VbeMode -notmatch '^[0-9A-Fa-f]{4}$') {
                throw ("Family $Family chip $($chip.Id) mode VbeMode " +
                       "'$($mode.VbeMode)' is not a four-digit hex number.")
            }
            # Deduplicated across chips as well as within one: two chips in a
            # family usually publish the same standard numbers, and querying a
            # mode twice spends a nested-execution BIOS call for nothing.
            $number = $mode.VbeMode.ToUpperInvariant()
            if (-not $probeModes.Contains($number)) { $probeModes.Add($number) }
        }
    }
    if ($probeModes.Count -eq 0) {
        throw "Family $Family declares no VBE mode numbers to probe."
    }
    if ($probeModes.Count -gt $probeLimit) {
        throw ("Family $Family needs $($probeModes.Count) rescue probes but " +
               "V9X_VBE_BASELINE_PROBE_MAX is $probeLimit. Raise the reserved " +
               "capacity in include\asm\V9XMAPI.INC and vbe_cache.h together, " +
               "or reduce the family's mode list - do not silently truncate.")
    }
    # Ascending, so the generated file is a function of the manifest's contents
    # and not of the order its chips happen to be written in.
    $probeSorted = @($probeModes | Sort-Object)
    Set-Content -LiteralPath $probeInclude -Encoding Ascii -Value @(
        "; Generated by scripts\build-minivdd-skeleton.ps1 from",
        "; packaging\families\$Family\family.psd1. Do not edit.",
        "; Bounded by V9X_VBE_BASELINE_PROBE_MAX = $probeLimit.",
        "V9xVbeProbeCount EQU $($probeSorted.Count)",
        ("V9xVbeProbeList dw " + (($probeSorted | ForEach-Object { $_ + "h" }) -join ", "))
    )
    $probeSummary = "$($probeSorted.Count) rescue probes from family ${Family}: " +
                    ($probeSorted -join ' ')

    # Assert the generated file against the manifest rather than trusting the
    # loop above: this include becomes BIOS calls at boot.
    $probeText = Get-Content -LiteralPath $probeInclude -Raw
    $emitted = @([regex]::Matches($probeText, '([0-9A-F]{4})h') |
                 ForEach-Object { $_.Groups[1].Value })
    $expected = @($probeSorted)
    if ($emitted.Count -ne $expected.Count -or
        @(Compare-Object $emitted $expected -SyncWindow 0).Count -ne 0) {
        throw "The generated rescue-probe list does not match family $Family."
    }
    if (@($emitted | Sort-Object -Unique).Count -ne $emitted.Count) {
        throw "The generated rescue-probe list contains a duplicate mode number."
    }
    # The CRLF matters here: -Raw keeps it, and $ in .NET multiline mode
    # stops before the newline rather than after the carriage return.
    $countPattern = "(?m)^V9xVbeProbeCount EQU " + $expected.Count + "\r?$"
    if ($probeText -notmatch $countPattern) {
        throw "The generated rescue-probe count does not match its list."
    }
}
Set-Content -LiteralPath $definitionFile -Encoding Ascii -Value @(
    "VXD V9XMINI DYNAMIC",
    "DESCRIPTION 'Velocity9x S3 ViRGE mini-VDD with monitor power management'",
    "SEGMENTS",
    "    _LTEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _LDATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _TEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _DATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    CONST CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _BSS CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _ITEXT CLASS 'ICODE' DISCARDABLE",
    "    _IDATA CLASS 'ICODE' DISCARDABLE",
    "EXPORTS",
    "    V9XMINI_DDB @1"
)

$sourcePath = Join-Path $repoRoot "src\minivdd32\loader.asm"
# include\asm holds V9XMAPI.INC, the mini-VDD API contract this and the 16-bit
# driver both assemble from. $outputDir holds the generated V9XBUILD.INC.
$asmIncludeDir = Join-Path $repoRoot "include\asm"
$assemblerArguments = @(
    "-coff", "-DBLD_COFF", "-W2", "-Zd", "-c", "-Cx",
    "-DMASM6", "-Sg", "-DVGA", "-DVGA31", "-DMINIVDD=1",
    "-I$ddkInclude", "-I$asmIncludeDir", "-I$outputDir",
    "-Fo$objectPath", $sourcePath
)
if ($DisableVbeCollect) {
    $assemblerArguments = @("-DV9X_NO_VBE_COLLECT") + $assemblerArguments
}
if ($ModeSweep) {
    if ($DisableVbeCollect) {
        throw "-ModeSweep needs the collection: the sweep walks its mode list."
    }
    $assemblerArguments = @("-DV9X_VBE_MODE_SWEEP") + $assemblerArguments
}
if ($VgaReturn) {
    if ($DisableVbeCollect) {
        throw ("-VgaReturn needs the collection: the mode set goes through " +
               "V9xMini_Vbe_Call, which no-ops without the V86 scratch the " +
               "collection allocates.")
    }
    $assemblerArguments = @("-DV9X_VGA_RETURN") + $assemblerArguments
}
if ($NoDpms) {
    $assemblerArguments = @("-DV9X_NO_DPMS") + $assemblerArguments
}
& $assembler @assemblerArguments
if ($LASTEXITCODE -ne 0) {
    throw "The Windows 98 DDK assembler failed to build the mini-VDD skeleton."
}

$linkerArguments = @(
    "/VXD", "/NOD", $objectPath, "/IGNORE:4078", "/IGNORE:4039",
    "/OUT:$vxdPath", "/MAP:$mapPath", "/DEF:$definitionFile"
)
& $linker @linkerArguments
if ($LASTEXITCODE -ne 0) {
    throw "The Windows 98 DDK linker failed to create the mini-VDD skeleton."
}

$bytes = [System.IO.File]::ReadAllBytes($vxdPath)
$newHeaderOffset = if ($bytes.Length -ge 64) {
    [System.BitConverter]::ToInt32($bytes, 0x3c)
} else { -1 }
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a -or
    $newHeaderOffset -lt 0 -or $newHeaderOffset + 1 -ge $bytes.Length -or
    $bytes[$newHeaderOffset] -ne 0x4c -or
    $bytes[$newHeaderOffset + 1] -ne 0x45) {
    throw "The mini-VDD output is not an MZ/LE image."
}

$imageText = [System.Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @("velocity9x:$BuildId", "V9X-MINI init",
                       "V9X-MINI power-callbacks-ok",
                       "V9X-MINI monitor-power D0", "V9XMINI_DDB")) {
    if (-not $imageText.Contains($marker)) {
        throw "The mini-VDD output is missing marker $marker."
    }
}
$disabledMarker = "V9X-MINI vbe-collect disabled"
if ($DisableVbeCollect) {
    if (-not $imageText.Contains($disabledMarker)) {
        throw "The no-collect mini-VDD is missing its disabled marker."
    }
} elseif ($imageText.Contains($disabledMarker)) {
    throw "A default mini-VDD build must not carry the vbe-collect disabled marker."
}
$sourceText = Get-Content -LiteralPath $sourcePath -Raw
# The audited four, plus the one PRE_HIRES_TO_VGA hook the -VgaReturn
# experiment adds. The count is over the source, so it has to account for the
# experimental line whether or not this build assembles it; the symbol check
# below is what ties the built image to the switch.
$expectedDispatches = if ($VgaReturn) { 5 } else { 4 }
$dispatchCount = ([regex]::Matches($sourceText, '(?m)^\s*MiniVDDDispatch\s+')).Count
if ($dispatchCount -ne $expectedDispatches -and $dispatchCount -ne 5) {
    throw "The mini-VDD source declares $dispatchCount dispatch entries; expected the four audited callbacks plus at most the PRE_HIRES_TO_VGA experiment."
}
if ($sourceText -notmatch 'MiniVDDDispatch\s+VESA_SUPPORT' -or
    $sourceText -notmatch 'MiniVDDDispatch\s+VESA_CALL_POST_PROCESSING' -or
    $sourceText -notmatch 'MiniVDDDispatch\s+SET_MONITOR_POWER_STATE' -or
    $sourceText -notmatch 'MiniVDDDispatch\s+GET_MONITOR_POWER_STATE_CAPS') {
    throw "The mini-VDD must install exactly the four audited monitor-power callbacks."
}
# The experimental hook may exist in the source only inside its IFDEF: a
# shipping build that installed it would be installing an untested BIOS call on
# the DOS-box path.
if ($sourceText -match '(?m)^\s*MiniVDDDispatch\s+PRE_HIRES_TO_VGA' -and
    $sourceText -notmatch '(?s)IFDEF\s+V9X_VGA_RETURN.*?MiniVDDDispatch\s+PRE_HIRES_TO_VGA.*?ENDIF') {
    throw "PRE_HIRES_TO_VGA is dispatched outside IFDEF V9X_VGA_RETURN."
}
$sweepSymbol = ([regex]::Matches(
    (Get-Content -LiteralPath $mapPath -Raw), 'V9xMini_Vbe_Sweep')).Count -gt 0
if ($ModeSweep -ne $sweepSymbol) {
    throw ("The mini-VDD image " + $(if ($sweepSymbol) { "carries" } else { "lacks" }) +
           " the mode sweep, which is not what -ModeSweep asked for.")
}

# Same rule for the two DOS-box experiments: the image has to carry exactly
# what the switch asked for, so a shipping package cannot pick one up by
# accident and an experimental package cannot silently lack it.
$vgaReturnSymbol = ([regex]::Matches(
    (Get-Content -LiteralPath $mapPath -Raw), 'MiniVDD_PreHiResToVGA')).Count -gt 0
if ($VgaReturn -ne $vgaReturnSymbol) {
    throw ("The mini-VDD image " + $(if ($vgaReturnSymbol) { "carries" } else { "lacks" }) +
           " the PRE_HIRES_TO_VGA hook, which is not what -VgaReturn asked for.")
}
$dpmsDisabledMarker = "V9X-MINI dpms-disabled"
if ($NoDpms) {
    if (-not $imageText.Contains($dpmsDisabledMarker)) {
        throw "The no-DPMS mini-VDD is missing its disabled marker."
    }
} elseif ($imageText.Contains($dpmsDisabledMarker)) {
    throw "A default mini-VDD build must not carry the DPMS disabled marker."
}
$mapText = Get-Content -LiteralPath $mapPath -Raw
foreach ($symbol in @("V9xMini_Serial_Write", "V9xMini_Set_Dpms",
                      "MiniVDD_VESASupport",
                      "MiniVDD_VESACallPostProcessing",
                      "MiniVDD_SetMonitorPowerState",
                      "MiniVDD_GetMonitorPowerStateCaps",
                      "MiniVDD_Dynamic_Init")) {
    if ($mapText -notmatch "(?m)^.*$([regex]::Escape($symbol)).*$") {
        throw "The mini-VDD map is missing symbol $symbol."
    }
}

Write-Output "Built boot-loadable monitor-power mini-VDD candidate: $vxdPath"
Write-Output "  $probeSummary"
