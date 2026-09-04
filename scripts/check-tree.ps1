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
    "docs\decisions\2026-08-26-gdi-accel-000.md",
    "docs\decisions\2026-08-26-gdi-accel-001.md",
    "docs\decisions\2026-08-27-gdi-accel-002.md",
    "docs\decisions\2026-08-27-gdi-accel-003.md",
    "docs\decisions\2026-08-27-gdi-accel-004-design.md",
    "docs\decisions\2026-08-27-crystalmark-barry-baseline.md",
    "docs\issues\2026-08-26-gdi-fill-brush-colour-not-physical.md",
    "docs\plans\gdi-acceleration.md",
    "docs\plans\gdi-accel-000-and-harness.md",
    "docs\decisions\2026-08-16-engine32-vtable.md",
    "docs\decisions\2026-08-16-s3-family-merge.md",
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
    "docs\plans\tier0-quality.md",
    "include\asm\V9XMAPI.INC",
    "include\velocity9x\s3_regs16.h",
    "packaging\win98se\INSTALL.TXT",
    "packaging\win98se\FIRSTBOOT.TXT",
    "packaging\win98se\RECOVER.TXT",
    # Family manifests are deliberately not listed here: they are discovered
    # by glob and schema-validated below, so a family is added by creating its
    # directory, with no script edit.
    "scripts\common.ps1",
    "scripts\lib\family.ps1",
    "scripts\lib\family-matrix.ps1",
    "scripts\lib\backend-registry.ps1",
    "scripts\update-backend-registry.ps1",
    "scripts\lib\inf.ps1",
    "scripts\audit-family-binary.ps1",
    "scripts\build-all-packages.ps1",
    "scripts\run-checks.ps1",
    "scripts\golden-baseline.ps1",
    "scripts\build-host.ps1",
    "scripts\build-host-msvc.ps1",
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
    # gdi_accel.c reads its SYSTEM.INI keys with GetPrivateProfileInt and
    # writes the deferred poison report with WritePrivateProfileString, so it
    # is a genuine OS boundary and is listed as one rather than reaching
    # <windows.h> through a header that already is.
    (Join-Path $repoRoot "src\display16\gdi_accel.c"),
    # The runtime mode table writes the validated inventory file
    # (C:\V9XDIAG\V9XMODES.INI) through WritePrivateProfileString; the table logic
    # itself stays in src\common\vbe_modes.c, which remains OS-free.
    (Join-Path $repoRoot "src\display16\modes16.c"),
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
foreach ($forbidden in @('v9x_mmio_write', 'v9x_mmio_read', 'V9X_VIRGE_', 'V9X_TRIO_')) {
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
foreach ($required in @('V9X_VBE_API_V2', 'V9X_VBE_MODE_LIST_MAX',
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

# Stage 1 is an exact v2 package pair. Reverting only the advertised version
# would make the indexed implementation unreachable while all layouts still
# agreed numerically, so assert the selected version as well as the constants.
if ($asmValues['V9XMINI_API_VERSION'] -ne $asmValues['V9XMINI_API_V2']) {
    throw "V9XMINI_API_VERSION must advertise the implemented v2 contract."
}
$miniSource = Get-Content -LiteralPath `
    (Join-Path $repoRoot "src\minivdd32\loader.asm") -Raw
if ($miniSource -notmatch '(?m)^\s*include\s+V9XPROBE\.INC\s*$') {
    throw "loader.asm does not consume the generated baseline rescue list."
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

$summaryFormat = "Velocity9x tree check passed ({0} source/header files, " +
                 "{1} families: {2}, {3} contract constants)."
Write-Output ($summaryFormat -f $sourceFiles.Count, $families.Count,
              (($families | ForEach-Object { $_.Id }) -join ', '),
              $contractChecked)
