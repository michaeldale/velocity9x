[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$required = @(
    "PLAN.md",
    "README.md",
    "CHANGELOG.md",
    "docs\vm-environment.md",
    "docs\plans\hellbender-hardware-d3d.md",
    "docs\plans\multi-chip-restructure.md",
    "docs\decisions\2026-08-08-vxd-lifecycle-probe.md",
    "docs\decisions\2026-08-08-active-640-candidate.md",
    "docs\specifications\win9x-driver-boundaries.md",
    "docs\specifications\family-manifest.md",
    "docs\decisions\2026-08-16-per-family-packaging.md",
    "docs\decisions\2026-08-16-restructure-baseline.md",
    "docs\decisions\2026-08-16-engine-fault-injection.md",
    "docs\decisions\2026-08-16-engine32-vtable.md",
    "docs\decisions\2026-08-16-s3-family-merge.md",
    "docs\specifications\logging-protocol.md",
    "docs\specifications\hardware-diagnostics.md",
    "include\velocity9x\backend.h",
    "include\velocity9x\build.h",
    "include\velocity9x\engine_abi.h",
    "include\velocity9x\hw16.h",
    "include\velocity9x\vbe_cache.h",
    "include\asm\V9XMAPI.INC",
    "include\velocity9x\s3_regs16.h",
    "packaging\win98se\INSTALL.TXT",
    "packaging\win98se\FIRSTBOOT.TXT",
    "packaging\win98se\RECOVER.TXT",
    "packaging\families\s3\family.psd1",
    "packaging\families\matrox-m2\family.psd1",
    "packaging\families\vbe\family.psd1",
    "packaging\families\ati\family.psd1",
    "scripts\common.ps1",
    "scripts\lib\family.ps1",
    "scripts\lib\family-matrix.ps1",
    "scripts\lib\inf.ps1",
    "scripts\audit-family-binary.ps1",
    "scripts\build-all-packages.ps1",
    "scripts\run-checks.ps1",
    "scripts\golden-baseline.ps1",
    "scripts\build-host.ps1",
    "scripts\build-host-msvc.ps1",
    "scripts\run-vm-mode-matrix.ps1",
    "scripts\update-associated-driver.ps1",
    "scripts\build-win16-skeleton.ps1",
    "scripts\build-win16-ddi-skeleton.ps1",
    "scripts\build-win16-loader-probe.ps1",
    "scripts\build-minivdd-skeleton.ps1",
    "scripts\build-active-package.ps1",
    "scripts\build-floppy-package.ps1",
    "scripts\build-settings.ps1",
    "scripts\build-settings-page.ps1",
    "scripts\build-gdi-smoke.ps1",
    "scripts\build-power-cycle.ps1",
    "scripts\build-palette-smoke.ps1",
    "scripts\backup-86box-profile.ps1",
    "scripts\build-dos-serial-smoke.ps1",
    "scripts\build-win32-serial-smoke.ps1",
    "scripts\build-vxd-loader-probe.ps1",
    "scripts\capture-serial-pipe.ps1",
    "scripts\prepare-vm-probe.ps1",
    "src\common\mode.c",
    "src\common\resources.c",
    "src\common\vbe_parse.c",
    "src\common\vbe_modes.c",
    "src\common\vbe_cache.c",
    "src\chipsets\generic\vbe\vbe_backend.c",
    "src\chipsets\generic\vbe\vbe_hw16.c",
    "src\chipsets\ati\ati_backend.c",
    "src\chipsets\ati\ati_hw16.c",
    "src\chipsets\ati\vt2\vt2_hw16.c",
    "src\chipsets\ati\mobility\mobility_hw16.c",
    "src\chipsets\s3\virge\backend.c",
    "src\chipsets\s3\virge\clocks.c",
    "src\chipsets\s3\virge\memory.c",
    "src\chipsets\s3\common\s3_regs16.c",
    "src\chipsets\s3\s3_hw16.c",
    "src\chipsets\s3\virge\virge_hw16.c",
    "src\chipsets\s3\trio64\trio_hw16.c",
    "src\chipsets\matrox\millennium2\mga2_hw16.c",
    "src\display16\display_component.c",
    "src\display16\loader.c",
    "src\display16\ddi.c",
    "src\display16\win9x_display_abi.h",
    "src\display32\ddhal_internal.h",
    "src\display32\ddhal_core.c",
    "src\display32\blt_cpu.c",
    "src\display32\engines\vga_scanout.c",
    "src\display32\engines\eng_s3_virge.c",
    "src\display32\engines\eng_s3_trio.c",
    "src\display32\d3d\d3d_virge.c",
    "src\display16\runtime.asm",
    "src\display16\dib_thunks.asm",
    "src\minivdd32\minivdd_component.c",
    "src\minivdd32\loader.asm",
    "tests\host\test_main.c",
    "tests\host\test_family_matrix.c",
    "tests\host\test_hw16_modes.c",
    "tests\host\test_vbe_parse.c",
    "tests\host\test_vbe_modes.c",
    "tests\host\test_vbe_cache.c",
    "tools\diag\serial_smoke.c",
    "tools\diag\serial_smoke_win32.c",
    "tools\diag\vxd_probe.asm",
    "tools\diag\vxd_probe_win32.c",
    "tools\diag\win16_driver_loader.c",
    "tools\diag\settings_win32.c",
    "tools\diag\settings_status.c",
    "tools\diag\settings_status.h",
    "tools\diag\settings_propsheet.c",
    "tools\diag\settings_propsheet.h",
    "tools\diag\settings_propsheet.rc",
    "tools\diag\gdi_smoke_win32.c",
    "tools\diag\power_cycle_win32.c",
    "tools\diag\palette_smoke_win32.c"
)

$missing = @($required | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $repoRoot $_))
})
if ($missing.Count -ne 0) {
    throw "Required repository files are missing: $($missing -join ', ')"
}

$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $repoRoot "src") -Recurse -File
$sourceFiles += Get-ChildItem -LiteralPath (Join-Path $repoRoot "include") -Recurse -File
$allowedOsBoundaries = @(
    (Join-Path $repoRoot "src\display16\loader.c"),
    (Join-Path $repoRoot "src\display16\ddi.c"),
    (Join-Path $repoRoot "src\display16\dd16.c"),
    (Join-Path $repoRoot "src\display16\enable16.c"),
    (Join-Path $repoRoot "src\display16\win9x_display_abi.h"),
    # The 32-bit HAL now has exactly one OS boundary: its private header. Every
    # translation unit of V9XHAL.DLL reaches <windows.h> through that and only
    # that, so a new HAL module cannot quietly acquire its own.
    (Join-Path $repoRoot "src\display32\ddhal_internal.h"),
    (Join-Path $repoRoot "src\minivdd32\loader.asm")
)
$forbidden = $sourceFiles |
    Select-String -Pattern '#include\s*[<"]windows\.h[>"]|#include\s*[<"]vmm\.h[>"]|include\s+(VMM|MINIVDD)\.INC' |
    Where-Object { $_.Path -notin $allowedOsBoundaries }
if ($forbidden) {
    $forbidden | ForEach-Object { Write-Error $_.ToString() }
    throw "Portable skeleton source contains an unapproved Windows/DDK dependency."
}

# Family manifests are data, so nothing else catches a typo in one until a
# build fails much later. Validate the schema and the cross-family invariants
# (unique PCI ownership, non-empty derived forbidden sets) here.
. (Join-Path $PSScriptRoot "lib\family.ps1")
$families = @(Get-V9xFamilies -RepoRoot $repoRoot)
if ($families.Count -eq 0) {
    throw "No family manifests found under packaging\families."
}
foreach ($family in $families) {
    foreach ($source in @($family.Build.Sources)) {
        if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $source.Path))) {
            throw "Family $($family.Id) references missing source $($source.Path)."
        }
    }
    $forbidden = @(Get-V9xFamilyForbiddenPatterns -Family $family -AllFamilies $families)
    $required = @(Get-V9xFamilyRequiredPatterns -Family $family)
    $overlap = @($forbidden | Where-Object { $_ -in $required })
    if ($overlap.Count -ne 0) {
        throw ("Family $($family.Id) both requires and forbids: " +
               ($overlap -join ', '))
    }
    if ($families.Count -gt 1 -and $forbidden.Count -eq 0) {
        throw ("Family $($family.Id) derives no forbidden patterns; its audit " +
               "cannot detect cross-family contamination.")
    }
}

# The mini-VDD API contract is written twice - once for the two assemblers
# (include\asm\V9XMAPI.INC) and once for their C consumers
# (include\velocity9x\vbe_cache.h) - because no assembler here reads C and no
# compiler here reads MASM. Nothing about a disagreement between the two is
# visible at build time: the driver would read a field from an offset the
# mini-VDD never wrote, at boot, on hardware. So the numbers are asserted equal
# here, and both assembly users are asserted to include the shared file rather
# than carry their own copy of any of it.
$asmContract = Join-Path $repoRoot "include\asm\V9XMAPI.INC"
$cContract = Join-Path $repoRoot "include\velocity9x\vbe_cache.h"

# EQU values in the shared include: a decimal, a MASM hex literal, another
# symbol, or the product of two of those. Anything else is deliberately not
# understood - the check should fail rather than guess.
function Resolve-V9xAsmValue {
    param([string]$Text, [hashtable]$Symbols, [string]$Name)
    $text = $Text.Trim()
    if ($text -match '^([^;]*?)\s*;.*$') { $text = $Matches[1].Trim() }
    if ($text -match '^(.+?)\s*\*\s*(.+)$') {
        $left = Resolve-V9xAsmValue -Text $Matches[1] -Symbols $Symbols -Name $Name
        $right = Resolve-V9xAsmValue -Text $Matches[2] -Symbols $Symbols -Name $Name
        return $left * $right
    }
    if ($text -match '^[0-9][0-9A-Fa-f]*[hH]$') {
        return [Convert]::ToInt32($text.Substring(0, $text.Length - 1), 16)
    }
    if ($text -match '^[0-9]+$') { return [int]$text }
    if ($Symbols.ContainsKey($text)) { return $Symbols[$text] }
    throw "Cannot evaluate $Name in V9XMAPI.INC: '$Text'."
}

$asmValues = @{}
foreach ($line in (Get-Content -LiteralPath $asmContract)) {
    if ($line -match '^\s*(V9X[A-Z0-9_]+)\s+EQU\s+(.+)$') {
        # Both captures are copied out before the resolver runs: it uses -match
        # itself, and $Matches is not worth sharing across a call.
        $constantName = $Matches[1]
        $constantText = $Matches[2]
        $asmValues[$constantName] = Resolve-V9xAsmValue -Text $constantText `
            -Symbols $asmValues -Name $constantName
    }
}

$cValues = @{}
foreach ($line in (Get-Content -LiteralPath $cContract)) {
    if ($line -match '^\s*#define\s+(V9X_VBE_[A-Z0-9_]+)\s+\(\(v9x_u16\)(0[xX][0-9A-Fa-f]+|[0-9]+)u\)') {
        $constantName = $Matches[1]
        $text = $Matches[2]
        $cValues[$constantName] = if ($text -match '^0[xX]') {
            [Convert]::ToInt32($text.Substring(2), 16)
        } else { [int]$text }
    }
}

# The version constants are the one pair whose names differ, because the C side
# never speaks the handshake itself.
$contractAliases = @{ 'V9X_VBE_API_V1' = 'V9XMINI_API_V1'
                      'V9X_VBE_API_V2' = 'V9XMINI_API_V2' }
$contractChecked = 0
foreach ($name in $cValues.Keys) {
    $asmName = if ($contractAliases.ContainsKey($name)) { $contractAliases[$name] } else { $name }
    if (-not $asmValues.ContainsKey($asmName)) {
        throw ("$name is defined in vbe_cache.h but $asmName is not defined in " +
               "V9XMAPI.INC; the two halves of the mini-VDD contract must agree.")
    }
    if ($asmValues[$asmName] -ne $cValues[$name]) {
        throw ("Mini-VDD contract mismatch: V9XMAPI.INC $asmName = " +
               "$($asmValues[$asmName]) but vbe_cache.h $name = $($cValues[$name]).")
    }
    $contractChecked++
}

# A renamed or deleted constant would otherwise shrink the checked set to
# nothing and still pass, so the load-bearing names are named here.
foreach ($required in @('V9X_VBE_API_V2', 'V9X_VBE_MODE_LIST_MAX',
                        'V9X_VBE_MODE_QUERY_MAX', 'V9X_VBE_CACHE_MAX',
                        'V9X_VBE_BASELINE_PROBE_MAX', 'V9X_VBE_EDID_BYTES',
                        'V9X_VBE_EDID_CHUNKS', 'V9X_VBE_RF_ORIGIN_LIST',
                        'V9X_VBE_RF_ORIGIN_PROBE', 'V9X_VBE_ST_LIST_VALID',
                        'V9X_VBE_ST_COLLECT_OFF')) {
    if (-not $cValues.ContainsKey($required)) {
        throw "vbe_cache.h no longer defines $required."
    }
}

# The packed record has to stay a power of two: both assembly users index it
# with a shift, and V9X_VBE_REC_SHIFT is what they shift by.
$recordBytes = $asmValues['V9X_VBE_REC_BYTES']
$recordShift = $asmValues['V9X_VBE_REC_SHIFT']
if (-not $recordBytes -or -not $recordShift -or
    [Math]::Pow(2, $recordShift) -ne $recordBytes) {
    throw ("V9XMAPI.INC record layout is inconsistent: V9X_VBE_REC_BYTES = " +
           "$recordBytes does not equal 1 shifted left by V9X_VBE_REC_SHIFT = " +
           "$recordShift.")
}
# Every field has to fit inside the record it is an offset into.
foreach ($field in @($asmValues.Keys | Where-Object {
            $_ -like 'V9X_VBE_REC_*' -and
            $_ -notin @('V9X_VBE_REC_BYTES', 'V9X_VBE_REC_SHIFT') })) {
    if ($asmValues[$field] -ge $recordBytes) {
        throw ("V9XMAPI.INC field $field is at offset $($asmValues[$field]), " +
               "outside the $recordBytes-byte record.")
    }
}

# The runtime table is sized to hold everything the cache can enumerate; if the
# cache grew past it, discovered modes would be dropped for no stated reason.
$modeTableMax = $null
foreach ($line in (Get-Content -LiteralPath (Join-Path $repoRoot "include\velocity9x\vbe_modes.h"))) {
    if ($line -match '^\s*#define\s+V9X_MODE_TABLE_MAX\s+\(\(v9x_u16\)([0-9]+)u\)') {
        $modeTableMax = [int]$Matches[1]
    }
}
if (-not $modeTableMax) {
    throw "vbe_modes.h no longer defines V9X_MODE_TABLE_MAX as a plain count."
}
if ($cValues['V9X_VBE_CACHE_MAX'] -gt $modeTableMax) {
    throw ("V9X_VBE_CACHE_MAX ($($cValues['V9X_VBE_CACHE_MAX'])) exceeds " +
           "V9X_MODE_TABLE_MAX ($modeTableMax); the runtime table could not " +
           "hold what the mini-VDD cache can report.")
}

# Both assembly users must reach these numbers through the shared include, and
# neither may shadow one with a local EQU. The forbidden set is exactly what the
# include defines, so adding a constant there extends this check by itself. A
# private constant that merely looks similar - loader.asm's V9X_VBE_CACHE_COUNT,
# the length of its own v1 mode list - is not in the set and is not the target.
foreach ($asmUser in @("src\minivdd32\loader.asm", "src\display16\runtime.asm")) {
    $asmText = Get-Content -LiteralPath (Join-Path $repoRoot $asmUser) -Raw
    if ($asmText -notmatch '(?m)^\s*include\s+V9XMAPI\.INC\s*$') {
        throw "$asmUser does not include V9XMAPI.INC."
    }
    foreach ($shared in $asmValues.Keys) {
        if ($asmText -match ('(?m)^\s*' + [regex]::Escape($shared) + '\s+EQU\s')) {
            throw ("$asmUser defines $shared locally; that constant belongs " +
                   "only in include\asm\V9XMAPI.INC.")
        }
    }
}

$summaryFormat = "Velocity9x tree check passed ({0} source/header files, " +
                 "{1} families: {2}, {3} contract constants)."
Write-Output ($summaryFormat -f $sourceFiles.Count, $families.Count,
              (($families | ForEach-Object { $_.Id }) -join ', '),
              $contractChecked)
