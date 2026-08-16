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
    "include\velocity9x\s3_regs16.h",
    "packaging\win98se\INSTALL.TXT",
    "packaging\win98se\FIRSTBOOT.TXT",
    "packaging\win98se\RECOVER.TXT",
    "packaging\families\s3\family.psd1",
    "packaging\families\matrox-m2\family.psd1",
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
    "src\chipsets\generic\vbe\vbe_backend.c",
    "src\chipsets\generic\vbe\vbe_hw16.c",
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
    "tests\host\test_vbe_parse.c",
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

$summaryFormat = "Velocity9x tree check passed ({0} source/header files, " +
                 "{1} families: {2})."
Write-Output ($summaryFormat -f $sourceFiles.Count, $families.Count,
              (($families | ForEach-Object { $_.Id }) -join ', '))
