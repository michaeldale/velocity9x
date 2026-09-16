[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$required = @(
    "PLAN.md",
    "README.md",
    "CHANGELOG.md",
    "docs\vm-environment.md",
    "docs\specifications\win9x-driver-boundaries.md",
    "docs\specifications\family-manifest.md",
    "docs\specifications\logging-protocol.md",
    "docs\specifications\hardware-diagnostics.md",
    "include\velocity9x\backend.h",
    "include\velocity9x\build.h",
    "include\velocity9x\engine_abi.h",
    "include\velocity9x\hw16.h",
    "include\velocity9x\vbe_cache.h",
    "include\velocity9x\mtrr.h",
    "src\common\mtrr.c",
    "tests\host\test_mtrr.c",
    "include\asm\V9XMAPI.INC",
    "include\velocity9x\s3_regs16.h",
    "packaging\win98se\INSTALL.TXT",
    "packaging\win98se\FIRSTBOOT.TXT",
    "packaging\win98se\RECOVER.TXT",
    "packaging\win98se\V9XCOPY.BAT",
    "packaging\win98se\V9XARM.BAT",
    # Family manifests are deliberately not listed here: they are discovered
    # by glob and schema-validated below, so a family is added by creating its
    # directory, with no script edit.
    "scripts\common.ps1",
    "scripts\lib\family.ps1",
    "scripts\lib\family-matrix.ps1",
    "scripts\lib\host-sources.ps1",
    "scripts\lib\backend-registry.ps1",
    "scripts\update-backend-registry.ps1",
    "scripts\lib\inf.ps1",
    "scripts\audit-family-binary.ps1",
    "scripts\build-all-packages.ps1",
    "scripts\run-checks.ps1",
    "scripts\golden-baseline.ps1",
    "scripts\build-host.ps1",
    "scripts\build-host-msvc.ps1",
    "scripts\check-intel-mmio-capture.ps1",
    "scripts\check-intel-gtt-capture.ps1",
    "scripts\check-intel-event-capture.ps1",
    "scripts\check-intel-ring-plan.ps1",
    "scripts\arm-intel-phase4.ps1",
    "scripts\run-vm-mode-matrix.ps1",
    "scripts\run-family-enable-gate.ps1",
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
    "src\common\backend_registry_table.inc",
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
    "src\display16\dd16.c",
    "src\display16\dd16.h",
    "src\display16\gdi_accel.c",
    "src\display16\gdi_accel.h",
    "src\common\d3dmode.c",
    "include\velocity9x\d3dmode.h",
    "tests\host\test_d3dmode.c",
    "src\display16\win9x_display_abi.h",
    "src\display32\ddhal_internal.h",
    "src\display32\ddhal_core.c",
    "src\display32\blt_cpu.c",
    "src\display32\engines\vga_scanout.c",
    "src\display32\engines\eng_s3_virge.c",
    "src\display32\engines\eng_s3_trio.c",
    "src\display32\d3d\d3d_internal.h",
    "src\display32\d3d\d3d_core.c",
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
    "src\chipsets\intel\i9xx_gtt.c",
    "tests\host\test_i9xx_gtt.c",
    "src\chipsets\intel\i9xx_ring.c",
    "tests\host\test_i9xx_ring.c",
    "src\chipsets\intel\i9xx_arm.c",
    "tests\host\test_i9xx_arm.c",
    "src\display16\intel_ring16.c",
    "src\display16\intel_exec16.c",
    "src\display16\intel_boot16.c",
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

# PowerShell 7 installs pwsh.exe under PSHOME, not powershell.exe. Controller
# launchers must resolve an available executable rather than constructing the
# Windows PowerShell name and silently treating that launch failure as an
# offline Win98 agent.
foreach ($script in @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "scripts") `
                         -Filter "*.ps1" -Recurse -File)) {
    if ((Get-Content -LiteralPath $script.FullName -Raw) -match
        'Join-Path\s+\$PSHOME\s+["'']powershell\.exe["'']') {
        throw "$($script.FullName) assumes powershell.exe exists under PSHOME."
    }
}

$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $repoRoot "src") -Recurse -File
$sourceFiles += Get-ChildItem -LiteralPath (Join-Path $repoRoot "include") -Recurse -File
$allowedOsBoundaries = @(
    (Join-Path $repoRoot "src\display16\loader.c"),
    (Join-Path $repoRoot "src\display16\ddi.c"),
    (Join-Path $repoRoot "src\display16\dd16.c"),
    (Join-Path $repoRoot "src\display16\enable16.c"),
    # gdi_accel.c reads its SYSTEM.INI keys with GetPrivateProfileInt and
    # writes the deferred poison report with WritePrivateProfileString, so it
    # is a genuine OS boundary and is listed as one rather than reaching
    # <windows.h> through a header that already is.
    (Join-Path $repoRoot "src\display16\gdi_accel.c"),
    # The runtime mode table writes the validated inventory file
    # (C:\V9XDIAG\V9XMODES.INI) through WritePrivateProfileString; the table logic
    # itself stays in src\common\vbe_modes.c, which remains OS-free.
    (Join-Path $repoRoot "src\display16\modes16.c"),
    # Intel Phase 1 writes its captured MMIO fingerprint with the same Win16
    # profile API. The decoder it calls remains OS-free in the chipset tree.
    (Join-Path $repoRoot "src\display16\intel_diag16.c"),
    (Join-Path $repoRoot "src\display16\intel_gtt16.c"),
    (Join-Path $repoRoot "src\display16\intel_event16.c"),
    # Phase 4's no-write plan publisher uses the Win16 profile API; packet
    # construction and validation remain OS-free in src\chipsets\intel.
    (Join-Path $repoRoot "src\display16\intel_ring16.c"),
    (Join-Path $repoRoot "src\display16\intel_exec16.c"),
    (Join-Path $repoRoot "src\display16\intel_boot16.c"),
    # Phase 5's sequencer writes INTEL3D0.TXT with the same Win16 profile API.
    # Every builder it calls stays OS-free in src\chipsets\intel.
    (Join-Path $repoRoot "src\display16\intel_3d16.c"),
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

# Every PCI id a family owns - chips and their aliases alike - is copied at
# load into the fixed DGROUP arrays runtime.asm's scan walks. ddi.c truncates
# to V9X_PCI_ID_LIMIT silently, so a manifest that outgrew the array would ship
# a driver that never scans for its last few ids and says nothing about it.
# Aliases are what make that reachable: they cost a manifest line each.
$pciIdLimit = $null
foreach ($line in (Get-Content -LiteralPath (Join-Path $repoRoot "src\display16\ddi.c"))) {
    if ($line -match '^\s*#define\s+V9X_PCI_ID_LIMIT\s+([0-9]+)u\s*$') {
        $pciIdLimit = [int]$Matches[1]
        break
    }
}
if (-not $pciIdLimit) {
    throw "src\display16\ddi.c no longer defines V9X_PCI_ID_LIMIT as a plain count."
}
foreach ($family in $families) {
    $idCount = @(Get-V9xFamilyPciEntries -Family $family).Count
    if ($idCount -gt $pciIdLimit) {
        throw ("Family $($family.Id) declares $idCount PCI ids, more than " +
               "V9X_PCI_ID_LIMIT ($pciIdLimit) in src\display16\ddi.c; the " +
               "scan table would be truncated silently.")
    }
}

# The backend registry's PCI dispatch table is generated from the manifests
# and checked in. Regenerate and compare, so a manifest edit that forgot
# scripts\update-backend-registry.ps1 fails here instead of shipping a
# registry that disagrees with the family that changed.
. (Join-Path $PSScriptRoot "lib\backend-registry.ps1")
$registryExpected = @(Get-V9xBackendRegistryTableLines -RepoRoot $repoRoot)
$registryPath = Join-Path $repoRoot "src\common\backend_registry_table.inc"
$registryActual = @(Get-Content -LiteralPath $registryPath)
if (Compare-Object -ReferenceObject $registryExpected -DifferenceObject $registryActual -SyncWindow 0) {
    throw ("src\common\backend_registry_table.inc does not match the family " +
           "manifests; run scripts\update-backend-registry.ps1 and commit the " +
           "result.")
}

# Which families may run the mini-VDD's boot-time VBE collection is derived from
# whether they can learn their aperture any other way, not from a hardcoded
# list. Both halves of the rule matter:
#
#   no read_aperture hook -> the collection is MANDATORY. Without it the 4F9Ch
#     cache is empty, there is no aperture, and the driver cannot enable. That
#     half is asserted per-family in Test-V9xFamilyManifest.
#   a read_aperture hook   -> the collection is FORBIDDEN. The cache is never
#     consulted, so eight nested Exec_Int 10h calls at boot are all risk and no
#     benefit - the 2026-08-18 gating decision, made after that code path hung a
#     physical Trio64.
#
# This used to be spelled "enabled only for vbe". That hardcoded list is what
# kept the ati family broken: ati has no hook, so disabling its collection made
# a package that could not enable, and the assertion held the bug in place
# rather than catching it (docs\issues\2026-08-26-ati-package-cannot-enable.md).
#
# The original concern behind the list - that a newly added family should not
# acquire boot-time BIOS calls by accident, since a missing key means enabled -
# is still met, and better: a new family with a hook is rejected for enabling,
# and a new family without one cannot work at all unless it is enabled, so the
# collection is never an accident either way.
foreach ($family in $families) {
    $hasHook = Test-V9xFamilyHasApertureHook -Family $family -RepoRoot $repoRoot
    $collects = $family.Build.MiniVddVbeCollect -ne $false
    if ($hasHook -and $collects) {
        throw ("Family $($family.Id) fills the read_aperture slot, so its " +
               "mini-VDD must assemble the VBE collection out " +
               "(Build.MiniVddVbeCollect = `$false): the 4F9Ch cache is never " +
               "consulted on such a family, so the boot-time BIOS calls are " +
               "all risk and no benefit.")
    }
    if (-not $hasHook -and -not $collects) {
        throw ("Family $($family.Id) has no read_aperture hook, so it must " +
               "keep the mini-VDD VBE collection; without it the family has no " +
               "aperture and cannot enable.")
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
                      'V9X_VBE_API_V2' = 'V9XMINI_API_V2'
                      'V9X_VBE_API_V3' = 'V9XMINI_API_V3'
                      'V9X_VBE_API_V4' = 'V9XMINI_API_V4'
                      'V9X_VBE_API_V5' = 'V9XMINI_API_V5'
                      'V9X_VBE_API_V6' = 'V9XMINI_API_V6'
                      'V9X_VBE_API_V7' = 'V9XMINI_API_V7' }
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

# The memory-type contract is the same two-file arrangement, with one
# difference that is the point of its design: only a handful of its constants
# are shared. The mini-VDD establishes the CPU flags and bounds its array, and
# every rule that reads them lives in host-tested C, so the C header defines a
# great deal the assembler never needs. Asserting the intersection, plus a
# named required set, is therefore the check - a blanket "every C constant must
# exist in the asm" would be wrong here rather than merely stricter.
$mtrrHeader = Join-Path $repoRoot "include\velocity9x\mtrr.h"
$mtrrValues = @{}
foreach ($line in (Get-Content -LiteralPath $mtrrHeader)) {
    if ($line -match '^\s*#define\s+(V9X_MTRR_[A-Z0-9_]+)\s+\(\(v9x_u16\)(0[xX][0-9A-Fa-f]+|[0-9]+)u\)') {
        $constantName = $Matches[1]
        $text = $Matches[2]
        $mtrrValues[$constantName] = if ($text -match '^0[xX]') {
            [Convert]::ToInt32($text.Substring(2), 16)
        } else { [int]$text }
    }
}
$mtrrShared = @('V9X_MTRR_CPU_CPUID', 'V9X_MTRR_CPU_MSR', 'V9X_MTRR_CPU_MTRR',
                'V9X_MTRR_CPU_PGE', 'V9X_MTRR_RANGE_MAX')
foreach ($name in $mtrrShared) {
    if (-not $mtrrValues.ContainsKey($name)) {
        throw "include\velocity9x\mtrr.h no longer defines $name."
    }
    if (-not $asmValues.ContainsKey($name)) {
        throw ("$name is defined in mtrr.h but not in V9XMAPI.INC; the two " +
               "halves of the memory-type contract must agree.")
    }
    if ($asmValues[$name] -ne $mtrrValues[$name]) {
        throw ("Memory-type contract mismatch: V9XMAPI.INC $name = " +
               "$($asmValues[$name]) but mtrr.h $name = $($mtrrValues[$name]).")
    }
    $contractChecked++
}
# The Direct3D core is chip-neutral, which is the whole claim of the
# core/engine split and exactly the kind of property that decays by one
# convenient exception. Two things must stay out of it: any MMIO access, and
# any chip's register vocabulary. Asserted here rather than left to the file's
# own header comment, and stated as a rule over the directory so a second
# engine is covered the day it is added rather than the day someone remembers.
$d3dCorePath = Join-Path $repoRoot "src\display32\d3d\d3d_core.c"
if (-not (Test-Path -LiteralPath $d3dCorePath)) {
    throw "src\display32\d3d\d3d_core.c is missing; the D3D core/engine split expects it."
}
$d3dCore = Get-Content -LiteralPath $d3dCorePath -Raw
foreach ($forbidden in @('v9x_mmio_write', 'v9x_mmio_read', 'V9X_VIRGE_',
                         'V9X_TRIO_', 'V9X_I9XX_')) {
    if ($d3dCore -match [regex]::Escape($forbidden)) {
        throw ("src\display32\d3d\d3d_core.c names $forbidden. The D3D core is " +
               "chip-neutral: register access and per-chip vocabulary belong " +
               "behind V9X_D3D_ENGINE_OPS in an engine file. See " +
               "docs\decisions\2026-08-29-d3d-core-engine-split.md.")
    }
}
# The other half of the same rule: an engine must not carry a DDHAL entry
# point. Those are the core's, and a chip file growing one is how the seam
# would quietly stop being a seam.
foreach ($engine in @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "src\display32\d3d") `
                        -Filter "d3d_*.c" |
                      Where-Object { $_.Name -ne 'd3d_core.c' })) {
    $text = Get-Content -LiteralPath $engine.FullName -Raw
    if ($text -match '(?m)^\s*DWORD\s+__stdcall\s+V9x') {
        throw ("src\display32\d3d\$($engine.Name) defines a DDHAL entry point. " +
               "Those belong in d3d_core.c; an engine implements " +
               "V9X_D3D_ENGINE_OPS and nothing else.")
    }
}

# The probe carries its own DirectDraw vocabulary on purpose - it must run
# against any driver - so Velocity9x's private render state number is written
# out twice. A copy that drifts would leave the instrument silently doing
# nothing, with the probe writing plausible keys from unforced draws.
$alphaForceHeader = Join-Path $repoRoot "include\velocity9x\win9x_ddraw_abi.h"
$alphaForceProbe = Join-Path $repoRoot "tools\diag\ddraw_probe_win32.c"
$alphaForceValues = @{}
foreach ($pair in @(
        @{ Path = $alphaForceHeader; Name = 'V9X_D3DRENDERSTATE_V9X_ALPHAFORCE' },
        @{ Path = $alphaForceProbe; Name = 'V9X_PROBE_RS_ALPHAFORCE' },
        @{ Path = $alphaForceHeader; Name = 'V9X_D3D_ALPHAFORCE_MAGIC' },
        @{ Path = $alphaForceProbe; Name = 'V9X_PROBE_ALPHAFORCE_MAGIC' })) {
    $pattern = "^\s*#define\s+$($pair.Name)\s+(0[xX][0-9A-Fa-f]+)ul\s*$"
    $found = $null
    foreach ($line in (Get-Content -LiteralPath $pair.Path)) {
        if ($line -match $pattern) {
            $found = [Convert]::ToUInt32($Matches[1], 16)
            break
        }
    }
    if ($null -eq $found) {
        throw ("$($pair.Name) is not defined in $($pair.Path). The private " +
               "render state is written out in both the ABI header and the " +
               "probe; removing it from one is a change that updates this " +
               "check with it.")
    }
    $alphaForceValues[$pair.Name] = $found
}
if ($alphaForceValues['V9X_D3DRENDERSTATE_V9X_ALPHAFORCE'] -ne
    $alphaForceValues['V9X_PROBE_RS_ALPHAFORCE']) {
    throw ("V9X_D3DRENDERSTATE_V9X_ALPHAFORCE and V9X_PROBE_RS_ALPHAFORCE " +
           "disagree: the driver would never see the state the probe sets.")
}
if ($alphaForceValues['V9X_D3D_ALPHAFORCE_MAGIC'] -ne
    $alphaForceValues['V9X_PROBE_ALPHAFORCE_MAGIC']) {
    throw ("V9X_D3D_ALPHAFORCE_MAGIC and V9X_PROBE_ALPHAFORCE_MAGIC " +
           "disagree: the driver would read the probe's argument as an " +
           "ordinary stipple pattern and force nothing.")
}

# Stage A writes no MTRR, and that is a property worth holding rather than
# trusting to review: the write instructions must not appear in the mini-VDD
# at all. WRMSR is the one that matters; CR0/CR4 handling would arrive with it.
$mtrrSource = Get-Content -LiteralPath `
    (Join-Path $repoRoot "src\minivdd32\loader.asm") -Raw
if ($mtrrSource -match '(?im)^\s*wrmsr\b') {
    throw ("src\minivdd32\loader.asm contains WRMSR. Stage A of " +
           "docs\plans\tier0-quality.md reads the memory-type registers and " +
           "writes none; adding the write is a staged change that updates " +
           "this check with it.")
}

# A renamed or deleted constant would otherwise shrink the checked set to
# nothing and still pass, so the load-bearing names are named here.
foreach ($required in @('V9X_VBE_API_V2', 'V9X_VBE_API_V3', 'V9X_VBE_API_V4',
                        'V9X_VBE_API_V5', 'V9X_VBE_API_V6',
                        'V9X_VBE_MODE_LIST_MAX',
                        'V9X_VBE_MODE_QUERY_MAX', 'V9X_VBE_CACHE_MAX',
                        'V9X_VBE_BASELINE_PROBE_MAX', 'V9X_VBE_EDID_BYTES',
                        'V9X_VBE_EDID_CHUNKS', 'V9X_VBE_RF_ORIGIN_LIST',
                        'V9X_VBE_RF_ORIGIN_PROBE', 'V9X_VBE_RF_ORIGIN_SWEEP',
                        'V9X_VBE_ST_SWEEP_RAN', 'V9X_VBE_ST_LIST_VALID',
                        'V9X_VBE_ST_COLLECT_OFF', 'V9X_VBE_ST_QUERY_LIMIT')) {
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
# private constant that merely looks similar - loader.asm's cautious Stage-1
# query clamp, for example - is not in the set and is not the target.
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

# The mini-VDD API is an exact v7 package pair. Reverting only the advertised version
# would make the indexed implementation unreachable while all layouts still
# agreed numerically, so assert the selected version as well as the constants.
if ($asmValues['V9XMINI_API_VERSION'] -ne $asmValues['V9XMINI_API_V7']) {
    throw "V9XMINI_API_VERSION must advertise the implemented v7 contract."
}
$miniSource = Get-Content -LiteralPath `
    (Join-Path $repoRoot "src\minivdd32\loader.asm") -Raw
if ($miniSource -notmatch '(?m)^\s*include\s+V9XPROBE\.INC\s*$') {
    throw "loader.asm does not consume the generated baseline rescue list."
}
# The mini-VDD is also used by the chip-agnostic VBE family. Its DPMS helper
# contains S3 extended-register writes, so the safe default must be a no-op and
# only the S3 family build may opt the body in. This is deliberately a positive
# guard: adding a family cannot make foreign register writes appear by default.
if ($miniSource -notmatch
    '(?ms)BeginProc\s+V9xMini_Set_Dpms\s*\r?\nIFNDEF\s+V9X_S3_DPMS\s*\r?\n\s*;[^\r\n]*\r?\n\s*ret\s*\r?\nELSE') {
    throw ("V9xMini_Set_Dpms must put its no-op path first behind " +
           "IFNDEF V9X_S3_DPMS; non-S3 images may not contain S3 writes.")
}
$miniBuildSource = Get-Content -LiteralPath `
    (Join-Path $repoRoot "scripts\build-minivdd-skeleton.ps1") -Raw
if ($miniBuildSource -notmatch
    '(?m)^\$s3Dpms = \(\$Family -eq ''s3''\) -and \(-not \$NoDpms\)\s*$' -or
    $miniBuildSource -notmatch
    '(?m)^\s*\$assemblerArguments = @\("-DV9X_S3_DPMS"\) \+ \$assemblerArguments\s*$') {
    throw ("build-minivdd-skeleton.ps1 must define V9X_S3_DPMS only for " +
           "the s3 family (and not for its -NoDpms experiment).")
}
# The Intel guards in the mini-VDD build. All three are derived from the
# family and none may be otherwise: a driver and a mini-VDD that disagree
# about which phases exist is a package pair that half-works, which is the
# failure mode the exact-match API version exists to prevent.
#
# V9X_I9XX_PHASE5_SUBMIT was behind a manual switch while no errata decision
# covered a 3D draw. The 2026-09-15 decision does, and B1 has to exercise
# the same binary B2 will run - which a manual switch would have prevented.
if ($miniBuildSource -notmatch
    '(?ms)^if \(\$intelMmio\) \{\s*\$assemblerArguments = @\("-DV9X_INTEL_MMIO_FINGERPRINT"\) \+ \$assemblerArguments\s*\$assemblerArguments = @\("-DV9X_I9XX_FIRST_WRITE_EXECUTOR"\) \+ \$assemblerArguments' -or
    [regex]::Matches($miniBuildSource,
        '"-DV9X_I9XX_FIRST_WRITE_EXECUTOR"').Count -ne 1) {
    throw "The Phase 4 mini-VDD executor must be defined exactly once, only for Intel."
}
if ([regex]::Matches($miniBuildSource,
        '"-DV9X_I9XX_PHASE5_SUBMIT"').Count -ne 1) {
    throw ("The Phase 5 mini-VDD submit guard must be defined exactly once.")
}
if ($miniBuildSource -match 'Phase5Submit') {
    throw ("The Phase 5 submit guard must not hang off a build switch. It " +
           "is family-derived like the Phase 4 executor, so that the " +
           "unarmed rehearsal boot exercises the same binary the armed " +
           "boot will run.")
}
$intelFamilySource = Get-Content -LiteralPath `
    (Join-Path $repoRoot 'packaging\families\intel-gma\family.psd1') -Raw
# Phase 5 joins the same list and is held to the same rule: compiled only
# for intel-gma, and never into runtime.asm. Being present in Defines means
# the sequencer and its submit path are BUILT - it does not mean anything is
# armed. A run still needs IntelArmPhase=5, the combined CRC covering both
# streams in execution order, and the one-shot token transfer, and
# arm-intel-phase5.ps1 refuses without the errata-gate decision on record.
if ($intelFamilySource -notmatch
    "Defines = @\('V9X_INTEL_GMA_FAMILY', 'V9X_I9XX_FIRST_WRITE_EXECUTOR',(?s).*?'V9X_I9XX_PHASE5_EXECUTOR',(?s).*?'V9X_I9XX_PHASE5_SUBMIT'\)" -or
    $intelFamilySource -match
    "RuntimeDefines = @\([^)]*V9X_I9XX_FIRST_WRITE_EXECUTOR" -or
    $intelFamilySource -match
    "RuntimeDefines = @\([^)]*V9X_I9XX_PHASE5_") {
    throw ("The Phase 4 and Phase 5 Win16 executors must be in the paired " +
           "Intel build only, and both Phase 5 guards must be present: the " +
           "driver and the mini-VDD must agree about which phases exist.")
}
if ($miniSource -notmatch
    '(?ms)^IFDEF\s+V9X_INTEL_MMIO_FINGERPRINT\s*\r?\n; EAX = current BAR0.*?^EndProc\s+V9xMini_I9xx_Capture.*?^EndProc\s+V9xMini_I9xx_Gtt_Capture.*?^EndProc\s+V9xMini_I9xx_Event_Capture.*?^EndProc\s+V9xMini_I9xx_Ring_Stage\s*\r?\n\s*IFDEF\s+V9X_I9XX_FIRST_WRITE_EXECUTOR.*?^EndProc\s+V9xMini_I9xx_Ring_Execute\s*\r?\nENDIF\s*\r?\nENDIF') {
    throw "The Intel capture, RAM staging and ring-write executor must remain behind positive guards."
}
$intelCapture = [regex]::Match(
    $miniSource,
    '(?ms)^BeginProc\s+V9xMini_I9xx_Capture\s*\r?\n(.*?)^EndProc\s+V9xMini_I9xx_Capture').Groups[1].Value
if ([regex]::Matches($intelCapture, '(?im)^\s*mov\s+eax,\s*\[esi\+ebx\]\s*$').Count -ne 2 -or
    $intelCapture -match '(?im)^\s*mov\s+\[esi\+ebx\]') {
    throw ("The Intel fingerprint must contain exactly two allowlist MMIO " +
           "reads and no MMIO write through ESI+EBX.")
}
$intelGttCapture = [regex]::Match(
    $miniSource,
    '(?ms)^BeginProc\s+V9xMini_I9xx_Gtt_Capture\s*\r?\n(.*?)^EndProc\s+V9xMini_I9xx_Gtt_Capture').Groups[1].Value
if ([regex]::Matches($intelGttCapture, '(?im)^\s*mov\s+eax,\s*\[edi\]\s*$').Count -ne 2 -or
    $intelGttCapture -match '(?im)^\s*mov\s+\[edi\]') {
    throw ("The Intel GTT fingerprint must contain exactly two full-table " +
           "MMIO read sites and no write through its mapped pointer.")
}
$intelEventCapture = [regex]::Match(
    $miniSource,
    '(?ms)^BeginProc\s+V9xMini_I9xx_Event_Capture\s*\r?\n(.*?)^EndProc\s+V9xMini_I9xx_Event_Capture').Groups[1].Value
if ([regex]::Matches($intelEventCapture,
        '(?im)^\s*mov\s+eax,\s*\[esi\+eax\]\s*$').Count -ne 2 -or
    [regex]::Matches($intelEventCapture,
        '(?im)^\s*mov\s+eax,\s*\[esi\]\s*$').Count -ne 2 -or
    $intelEventCapture -match '(?im)^\s*mov\s+\[esi(?:\+[^\]]+)?\]') {
    throw ("The Intel event journal must have exactly two ownership-MMIO and " +
           "two full-GTT read sites, with no write through either mapping.")
}
# The API v7 reserve hash is READ-ONLY, and that is the whole reason it may
# ship in an unarmed build. Same pattern as the three capture assertions
# above: a bounded number of read sites and no store through the
# register-indirect destination it walks.
#
# Two passes rather than one is a requirement, not an optimisation: an
# unstable read has to reach the caller as two different numbers.
$intelHashRange = [regex]::Match(
    $miniSource,
    '(?ms)^BeginProc\s+V9xMini_I9xx_Hash_Range\s*\r?\n(.*?)^EndProc\s+V9xMini_I9xx_Hash_Range').Groups[1].Value
if ($intelHashRange -eq '') {
    throw 'The mini-VDD has no V9xMini_I9xx_Hash_Range procedure.'
}
if ([regex]::Matches($intelHashRange, '(?im)^\s*mov\s+eax,\s*\[esi\]\s*$').Count -ne 1 -or
    $intelHashRange -match '(?im)^\s*mov\s+\[esi(?:\+[^\]]+)?\]') {
    throw ('V9xMini_I9xx_Hash_Range must read through ESI exactly once in its ' +
           'loop and must never write through it. It is the read-only verb ' +
           'that ships in the unarmed build.')
}
$intelHashCallers = [regex]::Matches(
    $miniSource, '(?im)^\s*call\s+V9xMini_I9xx_Hash_Range\s*$').Count
if ($intelHashCallers -ne 2) {
    throw ("The reserve hash must be invoked exactly twice - two independent " +
           "passes over the same bytes, both returned - but there are " +
           "$intelHashCallers call sites. An unstable read has to be visible " +
           'to the caller as two different numbers.')
}
if ($miniSource -notmatch '(?ms)^V9xMini_Api_I9xxRingHash:\s*\r?\n\s*IFDEF\s+V9X_INTEL_MMIO_FINGERPRINT') {
    throw ('The reserve hash verb must sit behind V9X_INTEL_MMIO_FINGERPRINT, ' +
           'not the executor guard: it ships in the unarmed build so that the ' +
           'unarmed boot proves the hash path before an armed boot needs it.')
}

$intelRingStage = [regex]::Match(
    $miniSource,
    '(?ms)^BeginProc\s+V9xMini_I9xx_Ring_Stage\s*\r?\n(.*?)^EndProc\s+V9xMini_I9xx_Ring_Stage').Groups[1].Value
if ([regex]::Matches($intelRingStage,
        '(?im)^\s*mov\s+\[edi\],\s*eax\s*$').Count -ne 1 -or
    [regex]::Matches($intelRingStage,
        '(?im)^\s*mov\s+\[edi\+ecx\*4\],\s*edx\s*$').Count -ne 1 -or
    $intelRingStage -match '(?im)^\s*mov\s+\[esi(?:\+[^\]]+)?\]') {
    throw "Intel ring staging may write only the fixed physical reserve, never MMIO."
}
$intelRingExecute = [regex]::Match(
    $miniSource,
    '(?ms)^BeginProc\s+V9xMini_I9xx_Ring_Execute\s*\r?\n(.*?)^EndProc\s+V9xMini_I9xx_Ring_Execute').Groups[1].Value
# One count per ring register, pinned so a new MMIO store cannot appear in
# the executor unnoticed. Phase 5's arms raised every one: 0203ch 3+2,
# 02034h 2+2, 02030h 6+4, 02038h 2+2.
#
# Phase 6's arm raised them again, by SIX in total and each one accounted
# for: four in its teardown, which returns CTL, HEAD, TAIL and START to zero
# after every scene, and two submissions - the probe boundary and the draw.
# The programming sequence is shared with Phase 5 rather than copied, which
# is why it adds none.
#
# Updated deliberately rather than relaxed. If a count changes again, the
# change is what to review - and the total below must keep accounting for
# every store, so a new one cannot hide behind a raised per-register count.
foreach ($store in @(
        @('0203ch', 6), @('02034h', 5), @('02030h', 13),
        @('02038h', 5))) {
    $pattern = '(?im)^\s*mov\s+dword ptr\s+\[esi\+' + $store[0] + '\],'
    if ([regex]::Matches($intelRingExecute, $pattern).Count -ne $store[1]) {
        throw ("Intel ring register store count changed at $($store[0]). " +
               "Every store to the ring registers is counted here so a " +
               "new one cannot be added without saying so.")
    }
}
if ([regex]::Matches($intelRingExecute,
        '(?im)^\s*mov\s+dword ptr\s+\[esi\+[^\]]+\],').Count -ne 29) {
    throw ("The Intel executor has an unreviewed MMIO store. The four "+
           "per-register counts above must account for every one: any "+
           "store to a register NOT in that list would pass them and "+
           "fail here.")
}
foreach ($dispatch in @(
        @('7', 'Wrap'), @('8', 'Reprobe'), @('9', 'Blt'))) {
    if ($intelRingExecute -notmatch
        ("(?m)^\s*cmp\s+ecx,\s*" + $dispatch[0] +
         "\s*\r?\n\s*je\s+V9xMini_I9xx_Ring_Execute_" +
         $dispatch[1] + "\s*$")) {
        throw "Intel Phase 4 ring step $($dispatch[0]) dispatch changed."
    }
}
if ($miniSource -notmatch
    '(?ms)^V9xMini_Api_I9xxRingDiag:\s*IFDEF\s+V9X_INTEL_MMIO_FINGERPRINT\s*cmp\s+V9xI9xxValid,\s*1\s*jne\s+short\s+V9xMini_Api_I9xxRingDiag_Missing\s*movzx\s+ecx,\s*\[ebp.Client_CX\]\s*cmp\s+ecx,\s*9\s*jae') {
    throw "Intel Phase 4 diagnostic allowlist must include CTL and START."
}
$intelRingSource = Get-Content -LiteralPath `
    (Join-Path $repoRoot 'src\display16\intel_ring16.c') -Raw
if ($intelRingSource -notmatch
    '(?ms)static WORD published_this_load;.*?if \(published_this_load != 0u\) \{ return; \}\s*published_this_load = 1u;') {
    throw "Intel Phase 4 ring log must survive later Enable diagnostics."
}
if ($miniSource -match '\bV9X_NO_DPMS\b' -or
    $miniBuildSource -match '\bV9X_NO_DPMS\b') {
    throw "The obsolete negative DPMS guard has returned; use V9X_S3_DPMS."
}

$intelHeader = Get-Content -LiteralPath `
    (Join-Path $repoRoot "include\velocity9x\intel_gma.h") -Raw
$snapshotCountMatch = [regex]::Match(
    $intelHeader,
    '(?m)^#define\s+V9X_I9XX_SNAPSHOT_DWORDS\s+\(\(v9x_u16\)([0-9]+)u\)\s*$')
if (-not $snapshotCountMatch.Success -or
    [int]$snapshotCountMatch.Groups[1].Value -ne
        $asmValues['V9X_I9XX_SNAPSHOT_DWORDS']) {
    throw ("The C and assembly Intel MMIO snapshot counts must agree with " +
           "V9X_I9XX_SNAPSHOT_DWORDS.")
}
$contractChecked++
$eventCountMatch = [regex]::Match(
    $intelHeader,
    '(?m)^#define\s+V9X_I9XX_EVENT_DWORDS\s+\(\(v9x_u16\)([0-9]+)u\)\s*$')
if (-not $eventCountMatch.Success -or
    [int]$eventCountMatch.Groups[1].Value -ne
        $asmValues['V9X_I9XX_EVENT_DWORDS']) {
    throw "The C and assembly Intel event record sizes must agree."
}
$contractChecked++
$gttCountMatch = [regex]::Match(
    $intelHeader,
    '(?m)^#define\s+V9X_I9XX_GTT_ENTRY_COUNT\s+\(\(v9x_u32\)([0-9]+)ul\)\s*$')
if (-not $gttCountMatch.Success -or
    [int]$gttCountMatch.Groups[1].Value -ne
        $asmValues['V9X_I9XX_GTT_ENTRY_COUNT']) {
    throw "The C and assembly Intel GTT entry counts must agree."
}
$contractChecked++
# Watcom's 16-bit convention makes SI callee-saved, and it keeps live pointer
# offsets there across calls. V9xFindPciDevice zeroes SI for INT 1Ah AX=B102h,
# so any far routine that reaches it must save SI or it silently corrupts the
# caller's next struct read. One omission cost three armed Phase 4 boots on
# 2026-09-14; the symptom was a refusal whose operands all looked correct.
$runtimeSource = Get-Content -LiteralPath `
    (Join-Path $repoRoot "src\display16\runtime.asm") -Raw
foreach ($proc in [regex]::Matches($runtimeSource,
        '(?ms)^(\w+)\s+PROC\s+FAR\s*\r?\n(.*?)^\1\s+ENDP')) {
    $body = $proc.Groups[2].Value
    if ($body -match '(?im)^\s*call\s+V9xFindPciDevice\s*$' -and
        $body -notmatch '(?im)^\s*push\s+e?si\s*$') {
        throw ("$($proc.Groups[1].Value) calls V9xFindPciDevice, which zeroes " +
               "SI, without saving SI. Watcom requires a callee to preserve " +
               "it and holds live pointer offsets there.")
    }
}

if ($miniSource -match '\bV9xVbeModeList\b|\bV9X_VBE_CACHE_COUNT\b') {
    throw "loader.asm still contains the removed fixed v1 mode cache."
}
$queryClampMatch = [regex]::Match(
    $miniSource, '(?m)^\s*V9X_STAGE1_QUERY_MAX\s+EQU\s+([0-9]+)\s*$')
if (-not $queryClampMatch.Success) {
    throw "loader.asm does not define its explicit Stage-1 BIOS-call clamp."
}
$queryClamp = [int]$queryClampMatch.Groups[1].Value
if ($queryClamp -le 0 -or $queryClamp -gt $asmValues['V9X_VBE_MODE_QUERY_MAX']) {
    throw ("loader.asm Stage-1 query clamp $queryClamp is outside the shared " +
           "V9X_VBE_MODE_QUERY_MAX bound.")
}

# Every SYSTEM.INI reader and writer must agree on which file and which
# section.
#
# gdi_accel.c reads the GdiAccel keys, dd16.c reads the Direct3D key,
# modes16.c reads the HighColor key, and the two settings surfaces read them
# back and write them - each from its own pair of
# literals rather than a shared header, because the header they would share
# would have to be reachable from src\common\d3dmode.c, which is deliberately
# free of every OS and DirectDraw dependency. Four files, one assertion: a typo
# in any of them would silently read defaults for ever, and the writer's typo
# would write a key nothing reads. Nothing else in the tree would notice
# either.
$settingsReaders = @{
    "src\display16\gdi_accel.c"     = @('V9X_SYSTEM_INI', 'V9X_INI_SECTION')
    "src\display16\dd16.c"          = @('V9X_SETTINGS_INI', 'V9X_SETTINGS_SECTION')
    "src\display16\modes16.c"       = @('V9X_SETTINGS_INI', 'V9X_SETTINGS_SECTION')
    "tools\diag\settings_status.c"  = @('V9X_SETTINGS_INI', 'V9X_SETTINGS_SECTION')
    "tools\diag\settings_propsheet.c" = @('V9X_SETTINGS_INI', 'V9X_SETTINGS_SECTION')
}
$settingsValues = @{}
foreach ($reader in $settingsReaders.Keys) {
    $readerText = Get-Content -LiteralPath (Join-Path $repoRoot $reader) -Raw
    foreach ($name in $settingsReaders[$reader]) {
        $match = [regex]::Match(
            $readerText,
            '(?m)^\s*#define\s+' + [regex]::Escape($name) + '\s+"([^"]*)"')
        if (-not $match.Success) {
            throw "$reader no longer defines $name as a string literal."
        }
        $role = if ($name -like '*SECTION*') { 'section' } else { 'file' }
        if ($settingsValues.ContainsKey($role) -and
            $settingsValues[$role] -cne $match.Groups[1].Value) {
            throw ("The SYSTEM.INI $role disagrees between the two readers: " +
                   "'$($settingsValues[$role])' against " +
                   "'$($match.Groups[1].Value)' in $reader.")
        }
        $settingsValues[$role] = $match.Groups[1].Value
    }
}

# The mode numbers the settings key accepts are the plan's mode numbers, and
# the plan is what a settings page or a recovery batch file would be written
# from. Assert the header still spells them the way that document does.
$d3dModeHeader = Get-Content -LiteralPath `
    (Join-Path $repoRoot "include\velocity9x\d3dmode.h") -Raw
foreach ($request in @(@('V9X_D3D_REQUEST_HARDWARE', 0),
                       @('V9X_D3D_REQUEST_DISABLED', 1),
                       @('V9X_D3D_REQUEST_SOFTWARE', 2),
                       @('V9X_D3D_REQUEST_HYBRID', 3),
                       @('V9X_D3D_REQUEST_OFFLOAD', 4))) {
    $pattern = '(?m)^\s*#define\s+' + [regex]::Escape($request[0]) +
               '\s+\(\(v9x_u16\)' + $request[1] + 'u\)'
    if ($d3dModeHeader -notmatch $pattern) {
        throw ("d3dmode.h no longer defines $($request[0]) as " +
               "$($request[1]); the SYSTEM.INI values are a documented " +
               "contract with docs\plans\s3-trio64-voodoo2-hybrid-3d.md.")
    }
}

# What an absent Direct3D= key means is a per-family build decision, and the
# two halves of it live in different files: the fallback in d3dmode.h and the
# override in a family manifest's Build.Defines. Neither half can see the
# other, and a mismatch is silent - a manifest naming a macro the header
# stopped honouring would compile and change nothing.
#
# The host suite asserts the fallback, because the host build takes no family
# define. Only a family compile has the override, so it is asserted here.
if ($d3dModeHeader -notmatch
    '(?m)^#ifndef\s+V9X_D3D_DEFAULT_REQUEST\r?\n#define\s+V9X_D3D_DEFAULT_REQUEST\s+V9X_D3D_REQUEST_HARDWARE\r?\n#endif') {
    throw ("d3dmode.h must define V9X_D3D_DEFAULT_REQUEST as " +
           "V9X_D3D_REQUEST_HARDWARE behind an #ifndef; a family manifest's " +
           "Build.Defines override depends on that guard, and every family " +
           "that sets nothing depends on the value.")
}
$d3dModeReader = Get-Content -LiteralPath `
    (Join-Path $repoRoot "src\display16\dd16.c") -Raw
if ($d3dModeReader -notmatch 'V9X_D3D_SETTING_KEY,\s*\r?\n\s*\(int\)V9X_D3D_DEFAULT_REQUEST,') {
    throw ("dd16.c must read the Direct3D key with V9X_D3D_DEFAULT_REQUEST " +
           "as the absent-key default, or a family's Build.Defines override " +
           "changes nothing on the machine.")
}
# The page preselects from the raw SYSTEM.INI value, so it has to learn the
# driver's default rather than apply its own; ddi.c publishes it and
# settings_status.c reads it back. One key name, two files, spelled here once.
foreach ($defaultKeyFile in @("src\display16\ddi.c",
                              "tools\diag\settings_status.c")) {
    $defaultKeyText = Get-Content -LiteralPath `
        (Join-Path $repoRoot $defaultKeyFile) -Raw
    if ($defaultKeyText -notmatch '"Direct3DDefault"') {
        throw ("$defaultKeyFile no longer names the V9XHW.INI " +
               "Direct3DDefault key; the settings page would then preselect " +
               "a Direct3D entry the driver did not choose.")
    }
}

# The one family that overrides the default today, asserted against the
# manifest rather than against a comment. Tier-0 has no 3D backend on any
# card, so this define is the difference between the CPU rasterizer and no
# Direct3D at all; see
# docs\decisions\2026-09-12-vbe-defaults-to-the-software-rasterizer.md.
$vbeManifestPath = Join-Path $repoRoot "packaging\families\vbe\family.psd1"
$vbeManifestText = Get-Content -LiteralPath $vbeManifestPath -Raw
if ($vbeManifestText -notmatch "V9X_D3D_DEFAULT_REQUEST=2") {
    throw ("The vbe family manifest must define V9X_D3D_DEFAULT_REQUEST=2: " +
           "the tier-0 package ships the software rasterizer on by default.")
}

# The packaged instructions are read on the target, in Notepad, on a machine
# whose display driver may be the thing that just failed. Notepad on Windows 9x
# does not break lines on a bare LF, so an LF-only file arrives as one
# unreadable line - reported from the field on FIRSTBOOT.TXT and RECOVER.TXT,
# which are copied into every package verbatim. Asserted rather than converted
# at packaging time so the repository copy is the readable one too.
foreach ($packagedText in @("packaging\win98se\INSTALL.TXT",
                            "packaging\win98se\FIRSTBOOT.TXT",
                            "packaging\win98se\RECOVER.TXT")) {
    $textPath = Join-Path $repoRoot $packagedText
    $bytes = [System.IO.File]::ReadAllBytes($textPath)
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if ($bytes[$i] -eq 0x0A -and ($i -eq 0 -or $bytes[$i - 1] -ne 0x0D)) {
            throw ("$packagedText has a bare LF at byte $i. Files copied into " +
                   "the package need CRLF or Notepad on the target shows one line.")
        }
    }
}

# ---------------------------------------------------------------------------
# The Intel code-segment boundary.
#
# The intel-gma family's 16-bit code is split across two CODE segments, so
# every call that crosses the boundary must be declared __far on both sides.
# include\velocity9x\intel16.h is the single place that says so; the whole
# point of having one place is that a second, unqualified declaration
# elsewhere is a mismatch, and a mismatched call is a wild jump rather than a
# diagnosable fault. Open Watcom's linker does refuse a near call across a
# class boundary (E2052), but only for a call it can see - a declaration that
# disagrees with the definition compiles to a far call to the wrong target
# without complaint. So the rule is enforced at source.
#
# See docs\plans\intel-gma950-phase5.md.
# ---------------------------------------------------------------------------
$boundaryHeaderPath = Join-Path $repoRoot "include\velocity9x\intel16.h"
if (-not (Test-Path -LiteralPath $boundaryHeaderPath)) {
    throw "include\velocity9x\intel16.h is missing; it owns the Intel segment boundary."
}
$boundaryHeaderText = Get-Content -LiteralPath $boundaryHeaderPath -Raw
$boundarySymbols = @([regex]::Matches($boundaryHeaderText,
    '\b(v9x_intel_publish_\w+|v9x_intel_boot_arm_prepare|v9x_intel_bridge_\w+|v9x_intel_str_\w+)\s*\(') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
# v9x_i9xx_sandbox_calculate is declared in intel_gma.h, which the other i9xx_
# entry points share; it carries the qualifier there for the same reason.
$boundarySymbols += 'v9x_i9xx_sandbox_calculate'
if ($boundarySymbols.Count -lt 6) {
    throw ("include\velocity9x\intel16.h declares only $($boundarySymbols.Count) " +
           "boundary symbol(s); the parse that finds them has stopped working.")
}
$boundaryOwners = @($boundaryHeaderPath,
                    (Join-Path $repoRoot "include\velocity9x\intel_gma.h"))
foreach ($file in $sourceFiles) {
    if ($file.FullName -in $boundaryOwners) { continue }
    $text = Get-Content -LiteralPath $file.FullName -Raw
    if (-not $text) { continue }
    foreach ($line in ($text -split "`r?`n")) {
        if ($line -notmatch '^\s*extern\b') { continue }
        foreach ($symbol in $boundarySymbols) {
            if ($line -match "\b$symbol\b") {
                throw ("$($file.Name) declares $symbol with a bare extern. It " +
                       "crosses the Intel code-segment boundary, so its one " +
                       "declaration lives in velocity9x\intel16.h - include " +
                       "that instead. A declaration without V9X_I9XX_FAR " +
                       "compiles a near call into a wild jump.")
            }
        }
    }
}
# The definition side: a function the header declares far must be defined far,
# or the compiler emits a near entry that the far call returns from wrongly.
foreach ($symbol in @($boundarySymbols | Where-Object { $_ -notlike 'v9x_intel_str_*' })) {
    $definitions = @($sourceFiles | Where-Object { $_.Extension -eq '.c' } |
        Select-String -Pattern "^[A-Za-z_].*\b$symbol\s*\(" -AllMatches)
    foreach ($definition in $definitions) {
        if ($definition.Line -match '^\s*extern\b') { continue }
        if ($definition.Line -notmatch '\bV9X_I9XX_FAR\b') {
            throw ("$($definition.Filename):$($definition.LineNumber) defines " +
                   "$symbol without V9X_I9XX_FAR, but velocity9x\intel16.h " +
                   "declares it far. The two must agree.")
        }
    }
}
# And the converse, which is the direction that actually bit.
#
# The two rules above assume the header is the starting point: they check that
# what intel16.h calls far is declared and defined far everywhere. Nothing
# checked a function the header does NOT name being defined far anyway.
#
# v9x_intel_phase5_run was, from the day it was written until 2026-09-15. Its
# only caller, intel_ring16.c, sits in I9XXCODE with it, so the call is near
# and its extern there is correctly near - but the definition returned with
# retf. That pops the caller's return offset as CS: a wild jump on entry to
# Phase 5, on the unarmed rehearsal boot as much as an armed one. Neither
# compiler saw both halves, and E2052 cannot fire on a call that is genuinely
# intra-segment, so it would have surfaced as an unexplained hang on the
# netbook.
#
# The qualifier is not decoration for "important Intel function". It means one
# specific thing, and this says so mechanically.
#
# A definition that splits its return type and name across two lines - as
# intel_bridge16.c does - is not judged here; the name is not on the line.
foreach ($file in ($sourceFiles | Where-Object { $_.Extension -eq '.c' })) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    if (-not $text) { continue }
    $lineNumber = 0
    foreach ($line in ($text -split "`r?`n")) {
        $lineNumber++
        if ($line -notmatch '\bV9X_I9XX_FAR\b') { continue }
        if ($line -match '^\s*(extern|#)') { continue }
        if ($line -notmatch '\bV9X_I9XX_FAR\b[\s\*]*\b(\w+)\s*\(') { continue }
        $defined = $matches[1]
        if ($defined -in $boundarySymbols) { continue }
        throw ("$($file.Name):$lineNumber defines $defined with " +
               "V9X_I9XX_FAR, but velocity9x\intel16.h does not declare it. " +
               "The qualifier marks a call that crosses into or out of " +
               "I9XXCODE, and the header is the one place that says which " +
               "calls those are. A far definition reached by the near call " +
               "its callers compile returns with retf and jumps into " +
               "nothing. Either add it to the header, or drop the qualifier.")
    }
}

# ---------------------------------------------------------------------------
# The generated Intel arm tables.
#
# check-tree needs no compiler, so it does the half of this that does not:
# the .inc exists, carries its generated banner, loader.asm no longer holds a
# literal table, and - the part that matters - PowerShell recomputes CRC-32
# over the parsed table and asserts it equals the EQU literal in the same file.
# That catches a hand edit of either the table or the CRC, because changing one
# without the other is exactly what a hand edit looks like.
#
# The other half - that the file matches what the compiled builders produce -
# is gen-intel-3d-stream.ps1 -Verify, which run-checks calls after build-host.
# ---------------------------------------------------------------------------
$intelIncPath = Join-Path $repoRoot 'src\minivdd32\i9xx3d.inc'
if (-not (Test-Path -LiteralPath $intelIncPath)) {
    throw ('src\minivdd32\i9xx3d.inc is missing. It is generated by ' +
           'scripts\gen-intel-3d-stream.ps1 and checked in so a hardware trip ' +
           'is reproducible from a clean checkout.')
}
$intelIncText = Get-Content -LiteralPath $intelIncPath -Raw
if ($intelIncText -notmatch '(?m)^; GENERATED FILE - DO NOT EDIT\.') {
    throw 'src\minivdd32\i9xx3d.inc has lost its generated banner.'
}

$loaderText = Get-Content -LiteralPath (Join-Path $repoRoot 'src\minivdd32\loader.asm') -Raw
if ($loaderText -match '(?m)^V9xI9xxRingExpected\s+dd\s') {
    throw ('src\minivdd32\loader.asm still defines the Phase 4 stream as a ' +
           'literal table. It must come from i9xx3d.inc, so that the mini-VDD ' +
           'and the driver cannot disagree about what was armed.')
}
if ($loaderText -notmatch '(?m)^include i9xx3d\.inc') {
    throw 'src\minivdd32\loader.asm does not include the generated i9xx3d.inc.'
}

# The RING_HEAD address mask, in the generated include and in the C header.
#
# It was a bare literal in loader.asm and nowhere else. A second submission
# path needs the same number, and a second copy of a constant with nothing
# comparing them is this project's most repeated defect - the executor's
# primitive boundary, the aperture read counts, and the packet offsets were all
# one number in two places. Tied here so it cannot become one again.
$intelHeadMaskInc = $null
if ($intelIncText -match '(?m)^V9X_I9XX_RING_HEAD_MASK\s+EQU\s+0([0-9a-fA-F]+)h\s*$') {
    $intelHeadMaskInc = [Convert]::ToUInt32($Matches[1], 16)
} else {
    throw ('src\minivdd32\i9xx3d.inc carries no V9X_I9XX_RING_HEAD_MASK. ' +
           'The mini-VDD masks RING_HEAD with it and the C side compares ' +
           'against the same value; a literal in one of them is how the two ' +
           'come to disagree.')
}
$intelGmaHeader = Get-Content -LiteralPath (Join-Path $repoRoot 'include\velocity9x\intel_gma.h') -Raw
if ($intelGmaHeader -match 'V9X_I9XX_RING_HEAD_MASK\s+\(\(v9x_u32\)0x([0-9a-fA-F]+)ul\)') {
    $intelHeadMaskC = [Convert]::ToUInt32($Matches[1], 16)
} else {
    throw 'includeelocity9x\intel_gma.h defines no V9X_I9XX_RING_HEAD_MASK.'
}
if ($intelHeadMaskInc -ne $intelHeadMaskC) {
    throw ('V9X_I9XX_RING_HEAD_MASK is ' + ('0x{0:X}' -f $intelHeadMaskInc) +
           ' in i9xx3d.inc and ' + ('0x{0:X}' -f $intelHeadMaskC) +
           ' in intel_gma.h. The mini-VDD and the HAL would disagree about ' +
           'which bits of RING_HEAD are an address.')
}
# And loader.asm must use the NAME rather than a literal, or the tie above
# checks a constant nothing reads.
if ($loaderText -match '(?m)and\s+eax,\s*0[0-9a-fA-F]*ffffch') {
    throw ('src\minivdd32\loader.asm masks RING_HEAD with a literal. Use ' +
           'V9X_I9XX_RING_HEAD_MASK from the generated include.')
}

function Get-V9xIncCrc32 {
    param([string[]]$Hex)
    [uint64]$crc = 0xffffffffL
    foreach ($text in $Hex) {
        [uint64]$value = [Convert]::ToUInt32($text, 16)
        for ($byte = 0; $byte -lt 4; ++$byte) {
            $crc = $crc -bxor ($value -band 0xffL)
            for ($bit = 0; $bit -lt 8; ++$bit) {
                if ($crc -band 1L) {
                    $crc = (($crc -shr 1) -bxor 0xedb88320L)
                } else {
                    $crc = $crc -shr 1
                }
            }
            $value = $value -shr 8
        }
    }
    return ('{0:X8}' -f [uint32]($crc -bxor 0xffffffffL))
}

# Parse a "LABEL DWORD" table out of the .inc, then the EQU it must agree with.
$intelIncLines = $intelIncText -split "`r?`n"
$intelTables = @{}
$currentLabel = $null
foreach ($line in $intelIncLines) {
    if ($line -match '^(\S+) LABEL DWORD$') {
        $currentLabel = $Matches[1]
        $intelTables[$currentLabel] = New-Object 'System.Collections.Generic.List[string]'
        continue
    }
    if ($currentLabel -and $line -match '^\s+dd\s+(.+)$') {
        foreach ($piece in ($Matches[1] -split ',')) {
            $trimmed = $piece.Trim()
            if ($trimmed -match '^0([0-9A-F]{8})h$') {
                $intelTables[$currentLabel].Add($Matches[1])
            } elseif ($trimmed -match '^OFFSET32 (\S+)$') {
                # The scene pointer directory. Recorded as the symbol rather
                # than refused: it is a dword table like the others, but its
                # dwords are link-time addresses and cannot be hex here.
                $intelTables[$currentLabel].Add($Matches[1])
            } else {
                throw "i9xx3d.inc has an unparsable dword '$trimmed' in $currentLabel."
            }
        }
        continue
    }
    if ($line.Trim() -eq '') { $currentLabel = $null }
}
# The Phase 6 executor must take every bound from the generated directories.
#
# Same rule as the Phase 5 boundaries below, and the same reason: a literal
# that was once correct is indistinguishable from one that still is. Here it
# would be worse, because each scene has a different length and primitive
# offset, so one literal cannot even be right for all five at once.
if ($loaderText -match 'V9xMini_I9xx_Ring_Execute_P6Verify') {
    foreach ($directory in @('V9xI9xxSceneDwords', 'V9xI9xxSceneCrc',
                             'V9xI9xxSceneTables', 'V9xI9xxScenePrim')) {
        if ($loaderText -notmatch [regex]::Escape($directory + '[edi*4]')) {
            throw ("src\minivdd32\loader.asm has a Phase 6 executor that never " +
                   "indexes $directory. Every per-scene bound comes from the " +
                   'generated directories, or the executor and the arm tables ' +
                   'can describe different scenes.')
        }
    }
    # The scene asked for must be checked against the scene that was staged.
    # A count alone would let one scene execute against another's staging.
    if ($loaderText -notmatch 'cmp\s+V9xI9xxSceneStagedFor, edi') {
        throw ('src\minivdd32\loader.asm does not check that the scene being ' +
               'executed is the scene that was staged. The staged COUNT alone ' +
               'does not establish which scene it counted.')
    }
    # And Phase 4 and 5 must clear the selection, so a stale scene index
    # cannot survive into a phase that never looks at it.
    if ($loaderText -notmatch 'mov\s+V9xI9xxExecScene, 0ffffffffh') {
        throw ('src\minivdd32\loader.asm never clears V9xI9xxExecScene. A ' +
               'Phase 6 selection left standing would be read by nothing and ' +
               'believed by the next capture.')
    }
}

# The executor's submission boundaries must come from the generated include.
#
# They were literals - 50 and 66 - correct for the 66-dword stream and silently
# wrong the moment the depth BUF_INFO removal made it 63 dwords with its
# primitive at 47. That submitted three dwords into the primitive and then drew
# through three nobody had staged. Nothing caught it, because a literal that
# used to be right looks exactly like a literal that still is.
foreach ($boundary in @(
    @{ Symbol = 'V9X_I9XX_P5_PRIMITIVE'; What = 'the probe/state submission' },
    @{ Symbol = 'V9X_I9XX_P5_DWORDS'; What = 'the draw submission' })) {
    if ($loaderText -notmatch [regex]::Escape($boundary.Symbol + ' * 4')) {
        throw ("src\minivdd32\loader.asm does not use $($boundary.Symbol) for " +
               "$($boundary.What). The boundary must come from i9xx3d.inc, " +
               'not be written here as a number that was once correct.')
    }
}
if ($loaderText -match '(?m)V9xI9xxRingWant,\s+V9X_I9XX_P5_RING_OFFSET \+ \d+ \* 4') {
    throw ('src\minivdd32\loader.asm submits to a literal ring offset. Use the ' +
           'generated V9X_I9XX_P5_PRIMITIVE and V9X_I9XX_P5_DWORDS.')
}

# Probe names must survive generation.
#
# They did not: the emitter's parser accepted only hex values, so every NAME=
# line was dropped and all fifty-two probes reached the committed data as
# Name = ''. Nothing failed, because nothing asked - which is why this asks.
#
# The capture keys ARE these names, so an unnamed probe is a row of hex nobody
# can attribute, and two probes sharing a name silently merge in the capture.
$intelDataPath = Join-Path $repoRoot 'scripts\data\intel-3d-stream.psd1'
if (Test-Path -LiteralPath $intelDataPath) {
    $intelData = Import-PowerShellDataFile -LiteralPath $intelDataPath
    if ($intelData.ContainsKey('Scenes')) {
        $sceneIndex = 0
        foreach ($sceneEntry in @($intelData.Scenes)) {
            $seen = @{}
            $probeIndex = 0
            foreach ($probe in @($sceneEntry.Probes)) {
                if ([string]::IsNullOrWhiteSpace($probe.Name)) {
                    throw ("scripts\data\intel-3d-stream.psd1 scene $sceneIndex " +
                           "probe $probeIndex has no name. The capture keys are " +
                           'those names; regenerate with gen-intel-3d-stream.ps1.')
                }
                if ($seen.ContainsKey($probe.Name)) {
                    throw ("scripts\data\intel-3d-stream.psd1 scene $sceneIndex " +
                           "uses the probe name '$($probe.Name)' twice. Two " +
                           'probes with one name merge in the capture.')
                }
                $seen[$probe.Name] = $true
                ++$probeIndex
            }
            if ($probeIndex -lt 1) {
                throw ("scripts\data\intel-3d-stream.psd1 scene $sceneIndex has " +
                       'no probes. A scene that reads nothing back measures ' +
                       'nothing.')
            }
            ++$sceneIndex
        }
    }
}

# The staging ring offset exists on both sides and they must agree.
#
# The submission boundaries are computed in C and written to a register by the
# assembler, and whether a boundary is qword aligned depends on this base as
# much as on the dword count. Two copies of it that could drift is how the
# alignment would come back.
$loaderRingOffset = $null
if ($loaderText -match '(?m)^V9X_I9XX_P5_RING_OFFSET\s+EQU\s+0([0-9A-Fa-f]+)h') {
    $loaderRingOffset = [Convert]::ToUInt32($Matches[1], 16)
}
$headerRingOffset = $null
$intelGmaText = Get-Content -LiteralPath (
    Join-Path $repoRoot 'include\velocity9x\intel_gma.h') -Raw
if ($intelGmaText -match '(?m)^#define\s+V9X_I9XX_P5_RING_OFFSET\s+\(\(v9x_u32\)0x([0-9A-Fa-f]+)ul\)') {
    $headerRingOffset = [Convert]::ToUInt32($Matches[1], 16)
}
if ($null -eq $loaderRingOffset -or $null -eq $headerRingOffset) {
    throw ('V9X_I9XX_P5_RING_OFFSET must be defined in both ' +
           'src\minivdd32\loader.asm and include\velocity9x\intel_gma.h.')
}
if ($loaderRingOffset -ne $headerRingOffset) {
    throw ("V9X_I9XX_P5_RING_OFFSET is 0x{0:X} in loader.asm and 0x{1:X} in " +
           'intel_gma.h. The submission boundaries are computed against one ' +
           'and written against the other.') -f $loaderRingOffset, $headerRingOffset
}
if (($loaderRingOffset -band 7) -ne 0) {
    throw ('V9X_I9XX_P5_RING_OFFSET is not qword aligned. RING_TAIL holds a ' +
           'qword-aligned offset and drops bit 2 - measured 2026-09-16 - so ' +
           'every submission from this base would be misaligned however the ' +
           'stream is padded.')
}

# Nobody may recompute the Phase 5 primitive offset.
#
# It was summed independently in four places - the builder, OffsetVertices, the
# vertex-bit reader and the emitter - and when the qword pad appeared they
# disagreed: the arm table said 48, the capture said 47, and the vertex reader
# published the _3DPRIMITIVE header as a coordinate.
#
# The driver is 16-bit code writing to a profile and cannot be exercised by a
# host test, so what is enforced instead is that it ASKS. A file that sums the
# prefix itself has reintroduced the defect whatever the number happens to be
# today.
foreach ($file in @('src\display16\intel_3d16.c',
                    'src\chipsets\intel\i9xx_scene.c',
                    'tests\host\test_main.c')) {
    $full = Join-Path $repoRoot $file
    if (-not (Test-Path -LiteralPath $full)) { continue }
    $text = Get-Content -LiteralPath $full -Raw
    # The prefix sum is fill + state + fragment program. Any file adding those
    # three together is deriving the boundary rather than asking for it.
    if ($text -match 'v9x_i9xx_phase5_fill_extent\(\)\s*\+\s*(?:\r?\n\s*)?v9x_i9xx_3d_state_extent\(\)\s*\+\s*(?:\r?\n\s*)?v9x_i9xx_fragment_program_extent\(\)\s*\+\s*\d') {
        throw ("$file recomputes the Phase 5 primitive offset by summing the " +
               'prefix. Call v9x_i9xx_phase5_primitive_offset instead: four ' +
               'copies of that sum is what published a primitive header as a ' +
               'vertex coordinate on 2026-09-16.')
    }
}

# EVERY generated submission boundary must be qword aligned.
#
# RING_TAIL holds a qword-aligned offset and drops bit 2. Measured on the part
# 2026-09-16: the mini-VDD wrote 0x10BC, read back 0x10B8, and the run was
# poisoned at scene 0. The old boundaries were aligned by ACCIDENT - 50 and 66
# dwords - and removing the depth BUF_INFO made them 47 and 63, breaking the
# Phase 5 path that had drawn correctly twice.
#
# Checked here rather than only in a host test because the value the executor
# uses is the generated one, and the arithmetic that has to hold is about the
# byte offset, not the dword count.
#
# Runs AFTER the ring-offset cross-check above, which is what establishes
# $headerRingOffset. Placed before it first, where the base read as zero and
# the alignment answer was right only because 0x1000 happens to be aligned -
# the reported address was 0xBC rather than 0x10BC, which is how it showed.
$boundaryChecks = New-Object 'System.Collections.Generic.List[object]'
if ($intelIncText -match '(?m)^V9X_I9XX_P5_DWORDS\s+EQU\s+(\d+)') {
    $boundaryChecks.Add(@{ What = 'V9X_I9XX_P5_DWORDS'; Dwords = [int]$Matches[1] })
}
if ($intelIncText -match '(?m)^V9X_I9XX_P5_PRIMITIVE\s+EQU\s+(\d+)') {
    $boundaryChecks.Add(@{ What = 'V9X_I9XX_P5_PRIMITIVE'; Dwords = [int]$Matches[1] })
}
foreach ($label in @('V9xI9xxSceneDwords', 'V9xI9xxScenePrim')) {
    if (-not $intelTables.ContainsKey($label)) { continue }
    $slot = 0
    foreach ($entry in @($intelTables[$label])) {
        $boundaryChecks.Add(@{ What = "$label[$slot]"
                               Dwords = [Convert]::ToInt32($entry, 16) })
        ++$slot
    }
}
if ($boundaryChecks.Count -lt 1) {
    throw 'i9xx3d.inc declares no submission boundaries to check.'
}
foreach ($check in $boundaryChecks) {
    $byteOffset = $headerRingOffset + ($check.Dwords * 4)
    if (($byteOffset -band 7) -ne 0) {
        throw ("i9xx3d.inc's $($check.What) is $($check.Dwords) dwords, which " +
               "puts RING_TAIL at 0x{0:X}. " -f $byteOffset) +
              ('That is not qword aligned: the register drops bit 2 and the ' +
               'tail read-back will not match what was written. Pad the ' +
               'stream - see ' +
               'docs\decisions\2026-09-16-intel-ring-tail-requires-qword-alignment.md.')
    }
}

# The Phase 6 scene directories, checked against the scene tables themselves.
#
# Three parallel arrays are three chances to disagree with the tables they
# describe, so each is recomputed here rather than trusted: the CRC from the
# table's own dwords, the length from its own entry count, and the pointer from
# its own label. A directory that drifted would arm the mini-VDD to execute one
# scene while gating on another's CRC.
if ($intelIncText -match '(?m)^V9X_I9XX_SCENE_COUNT\s+EQU\s+(\d+)') {
    $sceneCount = [int]$Matches[1]
    if ($intelIncText -notmatch '(?m)^V9X_I9XX_SCENE_AUTH\s+EQU\s+(\d+)') {
        throw 'i9xx3d.inc declares a scene count but no authorised-draw bound.'
    }
    $sceneAuth = [int]$Matches[1]
    if ($sceneCount -gt $sceneAuth) {
        throw ("i9xx3d.inc declares $sceneCount scenes but only $sceneAuth " +
               'draws are authorised. That bound is a recorded risk decision ' +
               'in docs\decisions\2026-09-15-intel-phase5-errata-gate.md, ' +
               'not a build parameter.')
    }
    if ($sceneCount -lt 1) { throw 'i9xx3d.inc declares no scenes.' }

    foreach ($label in @('V9xI9xxSceneDwords', 'V9xI9xxSceneCrc',
                         'V9xI9xxSceneTables')) {
        if (-not $intelTables.ContainsKey($label)) {
            throw "i9xx3d.inc has no $label directory."
        }
        if (@($intelTables[$label]).Count -ne $sceneCount) {
            throw ("i9xx3d.inc's $label has " +
                   "$(@($intelTables[$label]).Count) entries for " +
                   "$sceneCount scenes.")
        }
    }

    $sceneStreams = New-Object 'System.Collections.Generic.List[string]'
    for ($scene = 0; $scene -lt $sceneCount; ++$scene) {
        $label = "V9xI9xxScene${scene}Table"
        if (-not $intelTables.ContainsKey($label)) {
            throw "i9xx3d.inc has no $label."
        }
        $hex = @($intelTables[$label])
        $declared = [Convert]::ToInt32(
            @($intelTables['V9xI9xxSceneDwords'])[$scene], 16)
        if ($hex.Count -ne $declared) {
            throw ("i9xx3d.inc's $label holds $($hex.Count) dwords but its " +
                   "directory entry says $declared.")
        }
        $computed = Get-V9xIncCrc32 -Hex $hex
        $stated = @($intelTables['V9xI9xxSceneCrc'])[$scene]
        if ($computed -ne $stated) {
            throw ("i9xx3d.inc's $label hashes to $computed but its " +
                   "directory says $stated.")
        }
        if (@($intelTables['V9xI9xxSceneTables'])[$scene] -ne $label) {
            throw ("i9xx3d.inc's scene pointer $scene names " +
                   "$(@($intelTables['V9xI9xxSceneTables'])[$scene]), " +
                   "not $label.")
        }
        foreach ($dword in $hex) { $sceneStreams.Add($dword) }
    }

    # The combined CRC covers every scene's dwords in execution order, which is
    # what the arm token carries. Recomputed from the concatenation rather than
    # from the per-scene CRCs: a CRC of CRCs would not notice a scene changing
    # length while hashing the same.
    if ($intelIncText -notmatch '(?m)^V9X_I9XX_SCENE_CRC\s+EQU\s+0([0-9A-F]{8})h') {
        throw 'i9xx3d.inc has no V9X_I9XX_SCENE_CRC literal.'
    }
    $statedCombined = $Matches[1]
    $computedCombined = Get-V9xIncCrc32 -Hex @($sceneStreams)
    if ($computedCombined -ne $statedCombined) {
        throw ("i9xx3d.inc's scene tables hash to $computedCombined but " +
               "V9X_I9XX_SCENE_CRC says $statedCombined.")
    }
}

foreach ($pair in @(@{ Label = 'V9xI9xxPhase4Table'; Equ = 'V9X_I9XX_P4_PACKET_CRC'; Count = 'V9X_I9XX_P4_DWORDS' },
                    @{ Label = 'V9xI9xxPhase5Table'; Equ = 'V9X_I9XX_P5_CRC'; Count = 'V9X_I9XX_P5_DWORDS' })) {
    if (-not $intelTables.ContainsKey($pair.Label)) {
        throw "i9xx3d.inc has no $($pair.Label) table."
    }
    $hex = @($intelTables[$pair.Label])
    if ($intelIncText -notmatch ("(?m)^" + $pair.Equ + "\s+EQU\s+0([0-9A-F]{8})h")) {
        throw "i9xx3d.inc has no $($pair.Equ) literal."
    }
    $declared = $Matches[1]
    if ($intelIncText -notmatch ("(?m)^" + $pair.Count + "\s+EQU\s+([0-9]+)")) {
        throw "i9xx3d.inc has no $($pair.Count) literal."
    }
    if ([int]$Matches[1] -ne $hex.Count) {
        throw ("i9xx3d.inc says $($pair.Count) is $($Matches[1]) but " +
               "$($pair.Label) holds $($hex.Count) dwords.")
    }
    $recomputed = Get-V9xIncCrc32 -Hex $hex
    if ($recomputed -ne $declared) {
        throw ("i9xx3d.inc's $($pair.Equ) is $declared but CRC-32 over " +
               "$($pair.Label) is $recomputed. The table and its CRC disagree, " +
               'which is what a hand edit of a generated file looks like. Run ' +
               'scripts\gen-intel-3d-stream.ps1 and commit the result.')
    }
}
# The combined CRC must cover both phase CRCs in execution order, which is what
# a chained arm token carries.
if ($intelIncText -notmatch '(?m)^V9X_I9XX_COMBINED_CRC\s+EQU\s+0([0-9A-F]{8})h') {
    throw 'i9xx3d.inc has no V9X_I9XX_COMBINED_CRC literal.'
}
$combinedDeclared = $Matches[1]
$null = $intelIncText -match '(?m)^V9X_I9XX_P4_CRC\s+EQU\s+0([0-9A-F]{8})h'
$p4Declared = $Matches[1]
$null = $intelIncText -match '(?m)^V9X_I9XX_P5_CRC\s+EQU\s+0([0-9A-F]{8})h'
$p5Declared = $Matches[1]
$combinedRecomputed = Get-V9xIncCrc32 -Hex @($p4Declared, $p5Declared)
if ($combinedRecomputed -ne $combinedDeclared) {
    throw ("i9xx3d.inc's combined CRC is $combinedDeclared but CRC-32 over the " +
           "two phase CRCs is $combinedRecomputed.")
}

# ---------------------------------------------------------------------------
# Every refusal code a capture can carry must be one a reader can interpret.
#
# A capture is only worth the trip it saves if its numbers mean something, and
# the numbers live in two places: the C that emits them and
# hardware-diagnostics.md that explains them. Nothing kept those in step, and
# the drift was already there before this rule - PreconditionCode 13 had been
# emitted since the reserve grew and was in no table.
#
# The netbook has no serial port and no network path, so a refusal code that
# cannot be looked up is a second trip to the machine. That is the cost this
# prevents.
# ---------------------------------------------------------------------------
$diagnosticsPath = Join-Path $repoRoot "docs\specifications\hardware-diagnostics.md"
$diagnosticsText = Get-Content -LiteralPath $diagnosticsPath -Raw

# Phase 5's preconditions, published as two hex digits by v9x_p5_hex.
$phase5SourceText = Get-Content -LiteralPath `
    (Join-Path $repoRoot "src\display16\intel_3d16.c") -Raw
$phase5Codes = @([regex]::Matches($phase5SourceText,
    '(?m)^#define\s+V9X_P5_PRE_(\w+)\s+(\d+)u') | ForEach-Object {
        [pscustomobject]@{
            Name = $_.Groups[1].Value
            Hex  = '{0:X2}' -f [int]$_.Groups[2].Value
        }
    })
if ($phase5Codes.Count -lt 12) {
    throw ("Only $($phase5Codes.Count) V9X_P5_PRE_* codes were parsed from " +
           "intel_3d16.c; the parse that finds them has stopped working.")
}
# Capture to the next heading or end of file. The doc is CRLF, so nothing here
# may assume a bare \n - an earlier draft of this rule did and reported a
# missing table that was present.
$phase5Section = ''
if ($diagnosticsText -match '(?ms)### Phase 5 Precondition(.*?)(?=\r?\n### |\z)') {
    $phase5Section = $Matches[1]
} else {
    throw ("hardware-diagnostics.md has no '### Phase 5 Precondition' table. " +
           "Phase 5 publishes Precondition in its own code space and a reader " +
           "on a blind machine needs it.")
}
foreach ($code in $phase5Codes) {
    if ($phase5Section -notmatch "(?m)^\|\s*``$($code.Hex)``\s*\|") {
        throw ("V9X_P5_PRE_$($code.Name) publishes Precondition " +
               "``$($code.Hex)`` and hardware-diagnostics.md's Phase 5 table " +
               "has no row for it. A capture from the netbook cannot be read " +
               "without that row.")
    }
}
$phase5Rows = @([regex]::Matches($phase5Section, '(?m)^\|\s*`([0-9A-F]{2})`\s*\|') |
    ForEach-Object { $_.Groups[1].Value })
foreach ($row in $phase5Rows) {
    if ($row -notin @($phase5Codes | ForEach-Object { $_.Hex })) {
        throw ("hardware-diagnostics.md documents Phase 5 Precondition " +
               "``$row``, which intel_3d16.c does not define. A code that " +
               "cannot be emitted is worse than no row: it invites a reader " +
               "to explain a number the driver never produced.")
    }
}

# The chain's own rejection vocabulary, published as PreconditionChainReject
# and ChainDrawVerdict. Single digits, so they are matched in prose rather
# than in a table.
$chainCodes = @([regex]::Matches($intelHeader,
    '(?m)^#define\s+V9X_I9XX_CHAIN_REJECT_(\w+)\s+\(\(v9x_u16\)(\d+)u\)') |
    ForEach-Object {
        [pscustomobject]@{ Name = $_.Groups[1].Value; Value = $_.Groups[2].Value }
    })
if ($chainCodes.Count -lt 6) {
    throw ("Only $($chainCodes.Count) V9X_I9XX_CHAIN_REJECT_* codes were " +
           "parsed from intel_gma.h; the parse has stopped working.")
}
# Anchored at the start of the explaining paragraph, not at the first mention:
# the name also appears in the PreconditionCode table row that points here, and
# matching there caught the table's own digits instead of the vocabulary.
if ($diagnosticsText -notmatch
        '(?ms)^`PreconditionChainReject` is meaningful.*?(?=\r?\n\r?\n)') {
    throw ("hardware-diagnostics.md does not explain PreconditionChainReject, " +
           "which is the only thing that says why a chained arm refused.")
}
$chainProse = $Matches[0]
foreach ($code in $chainCodes) {
    if ($chainProse -notmatch "``$($code.Value)``") {
        throw ("V9X_I9XX_CHAIN_REJECT_$($code.Name) is ``$($code.Value)`` and " +
               "hardware-diagnostics.md's PreconditionChainReject paragraph " +
               "does not mention it.")
    }
}

# ---------------------------------------------------------------------------
# The armers' in-flight guard.
#
# It was inert from the day it was written, in both phases: FIND matches
# SUBSTRINGS, so "IntelInFlight=" matches the empty form and every non-empty
# one alike, and piping FIND into FIND /V on the same string always yields
# nothing. The guard never fired, and both armers would have overwritten the
# record of an unresolved run - the only evidence of where a hang stopped.
#
# It cannot be expressed with FIND on IntelInFlight at all, which is why the
# driver mirrors it into IntelIncomplete. This pins the replacement so it
# cannot quietly become inert again: both refusals must be present, and the old
# idiom must not return.
# ---------------------------------------------------------------------------
foreach ($armer in @('V9XARM.BAT', 'V9XARM5.BAT', 'V9XARM6.BAT')) {
    $armerPath = Join-Path $repoRoot "packaging\win98se\$armer"
    if (-not (Test-Path -LiteralPath $armerPath)) {
        throw "packaging\win98se\$armer is missing."
    }
    $armerText = Get-Content -LiteralPath $armerPath -Raw
    if ($armerText -match 'FIND\s+/V\s+"IntelInFlight=') {
        throw ("$armer has returned to piping FIND into FIND /V on " +
               '"IntelInFlight=". That never fires: FIND matches substrings, so ' +
               'the second FIND excludes every line the first selected.')
    }
    # A POSITIVE resolved value, not the absence of an unresolved one.
    # Testing only for the absence of =1 let IntelIncomplete= and
    # IntelIncomplete=garbage through: the key is present, it does not
    # contain =1, and execution reached the overwrite.
    if ($armerText -notmatch '(?m)^FIND "IntelIncomplete=0" .*\r?\n\s*IF ERRORLEVEL 1 GOTO UNRESOLVED') {
        throw ("$armer must require an explicit IntelIncomplete=0 and " +
               'refuse anything else - absent, empty or malformed. ' +
               'Inferring a resolved state from the absence of =1 accepts ' +
               'all three.')
    }
    if ($armerText -notmatch '(?m)^FIND "IntelIncomplete=1" .*\r?\n\s*IF NOT ERRORLEVEL 1 GOTO INFLIGHT') {
        throw "$armer must refuse an arm file whose IntelIncomplete is 1."
    }
}
# ---------------------------------------------------------------------------
# The chip's Direct3D word, against its engine capability and its source.
#
# These are three statements of one fact, and they drifted the moment a second
# engine existed. The intel-gma manifest carried EngineCaps = @('D3D') four
# lines below Direct3D = 'not-advertised', v9x_gma950_device carried a null
# where the word goes, and the settings page - which reads the word - offered
# no Hardware entry on the one card the engine was written for. Nothing
# compared them, so nothing objected.
#
# Direct3D= says what the silicon has. Whether a given boot may RUN it is
# IntelRuntime3D and the ring, and reaches the page through Direct3DMode=
# instead; these rules are about the static claim only.
# ---------------------------------------------------------------------------
foreach ($familyFile in Get-ChildItem -Path (
        Join-Path $repoRoot 'packaging\families') -Recurse -Filter 'family.psd1') {
    $familyData = Import-PowerShellDataFile -LiteralPath $familyFile.FullName
    $sourceByName = @{}
    foreach ($entry in @($familyData.Build.Sources)) {
        $sourceByName[$entry.Name] = $entry.Path
    }
    foreach ($chip in @($familyData.Chips)) {
        $word = [string]$chip.Direct3D
        $hasEngine = @($chip.EngineCaps) -contains 'D3D'
        $claimsHardware = $word -like 'hardware-*'
        if ($claimsHardware -and -not $hasEngine) {
            throw ("$($familyData.Id)/$($chip.Id): Direct3D = '$word' names an " +
                   'engine but EngineCaps does not carry D3D. The settings ' +
                   'page would offer Hardware on a chip the driver publishes ' +
                   'no Direct3D capability for.')
        }
        if ($hasEngine -and -not $claimsHardware) {
            throw ("$($familyData.Id)/$($chip.Id): EngineCaps carries D3D but " +
                   "Direct3D = '$word' claims no engine. The settings page " +
                   'reads that word, so the Hardware entry would be hidden on ' +
                   'a chip whose engine the driver implements.')
        }
        # And the word the driver actually writes to V9XHW.INI, which is a
        # string in the chip's own object rather than anything generated from
        # this manifest. The manifest documenting one word while the source
        # carried another is the same drift one level down.
        if (-not $claimsHardware) {
            continue
        }
        foreach ($objectName in @($chip.Objects)) {
            $objectPath = $sourceByName[$objectName]
            if (-not $objectPath) {
                continue
            }
            $objectText = Get-Content -Raw -LiteralPath (
                Join-Path $repoRoot $objectPath)
            if ($objectText -notmatch ('"' + [regex]::Escape($word) + '"')) {
                throw ("$($familyData.Id)/$($chip.Id): $objectPath does not " +
                       "carry the string `"$word`". The manifest is " +
                       'documentation; that string is what the driver writes ' +
                       'to V9XHW.INI and what the settings page reads.')
            }
        }
    }
}
# USED, not merely present. The rule above asks whether the chip's word
# appears in the chip's own object, and it did - while the family publisher
# wrote a string literal to V9XHW.INI instead, so the word never reached the
# file the settings page reads. A presence check is not a use check, and this
# is the second time that distinction has cost a boot on this feature.
foreach ($publisher in Get-ChildItem -Path (
        Join-Path $repoRoot 'src\chipsets') -Recurse -Filter '*.c') {
    $publisherText = Get-Content -Raw -LiteralPath $publisher.FullName
    foreach ($key in @('Direct3D', 'Acceleration')) {
        if ($publisherText -match ('write\("' + $key + '",\s*"')) {
            throw ("$($publisher.Name) writes the $key key as a string " +
                   'literal. It must publish the field the chip device carries, ' +
                   'or a chip that gains an engine keeps publishing the ' +
                   'word its publisher was written with.')
        }
    }
}

# The page must test the CONVENTION, not one engine's name. It compared
# against "hardware-s3d" literally, so "this card has a 3D engine" meant
# "this card is an S3" and every later engine read as having none.
$statusText = Get-Content -Raw -LiteralPath (
    Join-Path $repoRoot 'tools\diag\settings_status.c')
if ($statusText -notmatch
        'status->direct3d_capable = v9x_is_hardware_engine\(direct3d\);') {
    throw ('settings_status.c must derive direct3d_capable from the ' +
           '"hardware-" convention, not from one engine name.')
}
# And the reported sentence must be keyed on the RESOLVED mode, or a chip
# whose engine this boot did not permit is described as running it.
# Stated as a prohibition rather than a presence check, because the first
# draft of this rule was the latter and MISSED its mutation: removing the mode
# test from one branch left the other branch still matching. A presence check
# at the end of a stream is weaker than it looks.
if ($statusText -match '(?m)^\s*\} else if \(lstrcmpiA\(direct3d, "hardware') {
    throw ('settings_status.c has a hardware Direct3D sentence keyed on the ' +
           'chip word alone. That word is static - it says what the silicon ' +
           'has - so such a branch reports hardware Direct3D on a boot where ' +
           'the engine was never permitted. Key it on Direct3DMode= too.')
}

# ---------------------------------------------------------------------------
# The runtime Direct3D permission.
#
# Three separate things have to stay true together, and each was wrong once.
#
# IntelEnableThisBoot cannot stand in for any of them. It is per-arm, written
# to 0 at the top of every boot and to 1 only when a one-shot token is
# consumed, so a runtime boot has it clear by definition.
# ---------------------------------------------------------------------------
$gma950Text = Get-Content -Raw -LiteralPath (
    Join-Path $repoRoot 'src\chipsets\intel\gma950\gma950_hw16.c')
# RingOpen is a card-state write - RING_START and RING_CTL on a boot carrying
# no token. Reaching it because the BARs mapped put the one sequence that can
# start a GPU fetching behind no permission at all, on every DirectDraw
# session, with nothing to stop it from DOS afterwards.
if ($gma950Text -notmatch
        '(?m)^\s*if \(v9x_intel_runtime3d_allowed == 0u\) \{\r?\n(?:[^\r\n]*\r?\n)?\s*\} else if \(V9xMiniI9xxRingOpen\(') {
    throw ('gma950_hw16.c must gate V9xMiniI9xxRingOpen on ' +
           'v9x_intel_runtime3d_allowed. It writes ring registers on a boot ' +
           'with no arm token, and an ungated call has no off switch.')
}
# Tested again for the capability rather than inferred from the ring address
# being non-zero. They are the same fact only while the gate above is that
# address's only writer.
if ($gma950Text -notmatch
        '(?m)^\s*if \(v9x_intel_runtime3d_allowed != 0u && \*ring_linear_base != 0ul\) \{') {
    throw ('gma950_hw16.c must gate V9X_DD_ENGINE_CAP_D3D on ' +
           'v9x_intel_runtime3d_allowed as well as on the ring address.')
}
$boot16Text = Get-Content -Raw -LiteralPath (
    Join-Path $repoRoot 'src\display16\intel_boot16.c')
# Every path out of arm_prepare past the arm transaction is a return, and each
# describes the state of the ARM. Reading the key after them would leave the
# runtime permission unanswered on all of them.
if ($boot16Text -notmatch
        '(?s)IntelRuntime3D.*?v9x_intel_runtime3d_allowed = 1u;.*?IntelEnableThisBoot", "0"') {
    throw ('intel_boot16.c must read IntelRuntime3D before the arm ' +
           'transaction. Every later exit is a return describing the arm, ' +
           'not the runtime path.')
}
# An armed boot owns the ring: it sets START and CTL itself and clears them on
# teardown. Both arming paths must drop the runtime permission, or a
# DirectDraw session brings the ring up underneath a diagnostic.
if (@([regex]::Matches($boot16Text,
        '(?m)^\s*v9x_intel_boot_arm_latch = 1u;\r?\n(\s*/\*[^\r\n]*\*/\r?\n)?\s*v9x_intel_runtime3d_allowed = 0u;')).Count -ne 2) {
    throw ('Both arming paths in intel_boot16.c must clear ' +
           'v9x_intel_runtime3d_allowed. An armed boot owns the ring.')
}
# v9x_d3d_publish_engine() selects on engine_type at DriverInit, and the
# refresh that used to be its only writer runs strictly later. Unstamped, the
# field read zero, and zero falls back to the binary's one hardware engine -
# the ViRGE - so an Intel part published the ViRGE's device description.
$dd16Text = Get-Content -Raw -LiteralPath (
    Join-Path $repoRoot 'src\display16\dd16.c')
# The BODY, not the file. A lazy match across the whole file ran past this
# function into v9x_dd_refresh_framebuffer, which has the same assignment -
# so the rule passed on the unstamped source it was written to catch. A
# presence check at the end of a stream is weaker than it looks.
$stampBody = [regex]::Match($dd16Text,
    '(?sm)^static void v9x_dd_stamp_engine_caps.*?^\}')
if (-not $stampBody.Success) {
    throw 'v9x_dd_stamp_engine_caps could not be located in dd16.c.'
}
if ($stampBody.Value -notmatch 'shared->engine\.engine_type = engine_type;') {
    throw ('v9x_dd_stamp_engine_caps must stamp engine_type. It runs before ' +
           'DriverInit and v9x_d3d_publish_engine selects on that field; ' +
           'zero there publishes the ViRGE device description for any chip.')
}
# The DOS switch, which is the only thing that can turn the permission on and
# the only thing besides V9XCOPY that can turn it off.
$switchPath = Join-Path $repoRoot 'packaging\win98se\V9X3D.BAT'
if (-not (Test-Path -LiteralPath $switchPath)) {
    throw ('packaging\win98se\V9X3D.BAT is missing. Without it the runtime ' +
           'permission can be granted but not revoked from DOS.')
}
$switchText = Get-Content -Raw -LiteralPath $switchPath
foreach ($value in @('IntelRuntime3D=1', 'IntelRuntime3D=0')) {
    if ($switchText -notmatch ('(?m)^ECHO ' + [regex]::Escape($value) + '>>')) {
        throw "V9X3D.BAT must write $value."
    }
}
# COMMAND.COM has no IF /I, and a syntax error there does not halt a batch -
# the next line simply runs, which is how a guard becomes its own opposite.
if ($switchText -match '(?m)^\s*IF\s+/I\b') {
    throw 'V9X3D.BAT uses IF /I, which COMMAND.COM does not support.'
}
# OFF is the recovery direction. A recovery command that can decline to run is
# not one, so it must not sit behind the in-flight refusal that ON uses.
if ($switchText -notmatch '(?s):WANTOFF.*?ECHO IntelRuntime3D=0>>') {
    throw ('V9X3D.BAT must write the OFF value without an intervening ' +
           'refusal. Disabling the runtime path is the recovery path.')
}

# And the reset must write the flag, or a freshly reset file reads as legacy.
$copyText = Get-Content -LiteralPath (Join-Path $repoRoot 'packaging\win98se\V9XCOPY.BAT') -Raw
if ($copyText -notmatch '(?m)^ECHO IntelIncomplete=0>>') {
    throw ('V9XCOPY.BAT must write IntelIncomplete=0 when it resets the arm ' +
           'state, or the armers refuse the file it just wrote as ambiguous.')
}
# The driver must set the flag BEFORE the value it mirrors, and clear it
# AFTER. These are separate profile writes with nothing making them atomic, so
# the order is the safety property: interrupted either way must leave the flag
# set, which refuses, rather than clear, which would bypass the stop.
$bootText = Get-Content -LiteralPath (Join-Path $repoRoot 'src\display16\intel_boot16.c') -Raw
# Stated as a prohibition, not a requirement. A positive match is satisfied by
# any one correctly ordered site, so with two arming paths - one-shot and
# repeat - it passed while one of them was swapped. Forbidding the wrong order
# catches every site.
if ($bootText -match '(?s)v9x_intel_boot_set\("IntelInFlight", arm_once\).{0,300}?v9x_intel_boot_set\("IntelIncomplete", "1"\)') {
    throw ('intel_boot16.c writes IntelInFlight before IntelIncomplete=1 on ' +
           'some path. The flag must be set FIRST: the reverse order leaves a ' +
           'window where a token is in flight with the flag clear, which is a ' +
           'silent bypass of the hang stop.')
}
if ($bootText -notmatch '(?s)v9x_intel_boot_set\("IntelIncomplete", "1"\).{0,200}?v9x_intel_boot_set\("IntelInFlight", arm_once\)') {
    throw ('intel_boot16.c must set IntelIncomplete=1 before writing ' +
           'IntelInFlight on at least one path; neither was found.')
}
if ($bootText -notmatch '(?s)v9x_intel_boot_set\("IntelInFlight", ""\).{0,1200}?v9x_intel_boot_set\("IntelIncomplete", "0"\)') {
    throw ('intel_boot16.c must clear IntelInFlight BEFORE IntelIncomplete, ' +
           'so an interrupted retirement leaves the flag set and refuses ' +
           'rather than clear and bypasses.')
}

$summaryFormat = "Velocity9x tree check passed ({0} source/header files, " +
                 "{1} families: {2}, {3} contract constants)."
Write-Output ($summaryFormat -f $sourceFiles.Count, $families.Count,
              (($families | ForEach-Object { $_.Id }) -join ', '),
              $contractChecked)
