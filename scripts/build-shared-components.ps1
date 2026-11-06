# Build the parts of a family package that no family manifest changes: the
# DirectDraw HAL, the OpenGL ICD, the Glide and 3DCIF DLLs, and the
# diagnostic and update tools.
#
# Each of these writes one shared folder under build\ and takes nothing but
# the build id, so its output is the same for every family.
# build-active-package.ps1 used to rebuild them for each family; with eight
# families that was 13 of every 16 seconds a family took, and most of
# run-checks.ps1. build-all-packages.ps1 now runs this once and passes
# -SkipShared to each family. A standalone build-active-package.ps1 still
# runs it, so a single family's package is never assembled from stale files.
#
# What stays per family is what reads the manifest: the Win16 driver, the
# mini-VDD and the two settings binaries (Package.ModesSummary).
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuildId,
    [string]$DdkRoot = "C:\98DDK"
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "build-gdi-smoke.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-palette-smoke.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-mode-switch.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-power-cycle.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-ddraw-probe.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-trace-dump.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-update.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-window-list.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-ddraw-hal-dll.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-opengl-icd.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-glide.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-glide3.ps1") -BuildId $BuildId
# Every family package carries ATI3DCIF.DLL, but only run-checks.ps1 built
# it, so a family built on its own copied whatever an earlier run had left.
& (Join-Path $PSScriptRoot "build-cif.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-vxd-loader-probe.ps1") `
    -BuildId $BuildId -DdkRoot $DdkRoot
& (Join-Path $PSScriptRoot "build-win16-loader-probe.ps1") -BuildId $BuildId
& (Join-Path $PSScriptRoot "build-driver-stage-probe.ps1") -BuildId $BuildId
# v9xinfplan.exe, which each family's INF is checked against.
& (Join-Path $PSScriptRoot "build-release-tools.ps1") | Out-Null
