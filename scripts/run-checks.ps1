# The local CI gate for Velocity9x.
#
# check-tree -> host tests -> per-family builds, audits and INF assertions ->
# floppy. Every phase of docs\plans\multi-chip-restructure.md ends with this
# green. -GoldenCompare additionally re-runs the byte comparison that phases
# 1-7 require.
[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = "C:\98DDK",
    [string[]]$Family,
    # Phases 1-7 only: assert the shipped images are byte-for-byte what the
    # captured baseline recorded. Implies a pinned BuildId.
    [switch]$GoldenCompare,
    [string]$GoldenBuildId = "golden-compare",
    [switch]$SkipHostTests
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")

if ($GoldenCompare -and $BuildId -and $BuildId -ne $GoldenBuildId) {
    throw "A golden compare must build with -BuildId $GoldenBuildId."
}
if ($GoldenCompare) {
    $BuildId = $GoldenBuildId
}
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "checks-local"
}

$steps = @()
function Invoke-CheckStep {
    param([string]$Name, [scriptblock]$Body)
    Write-Output "==> $Name"
    & $Body
    $script:steps += $Name
}

Invoke-CheckStep "check-tree" {
    & (Join-Path $PSScriptRoot "check-tree.ps1")
}

# The survey's source gate is the reason that tool can be handed to a stranger,
# so it is checked continuously rather than at the moment it was written. This
# runs the gate against deliberately broken copies of the source and needs no
# compiler, so it costs a fraction of a second.
Invoke-CheckStep "vga survey safety gate" {
    & (Join-Path $PSScriptRoot "build-vga-survey.ps1") -GateSelfTest
}

if (-not $SkipHostTests) {
    Invoke-CheckStep "host tests" {
        & (Join-Path $PSScriptRoot "build-host.ps1")
    }
}

# build-all-packages runs each family's builder, which links, audits through
# audit-family-binary.ps1, generates and asserts the INF, and assembles the
# floppy from the manifests.
Invoke-CheckStep "family packages" {
    $arguments = @{ BuildId = $BuildId; DdkRoot = $DdkRoot }
    if ($Family) { $arguments['Family'] = $Family }
    & (Join-Path $PSScriptRoot "build-all-packages.ps1") @arguments
}

if ($GoldenCompare) {
    Invoke-CheckStep "golden compare" {
        & (Join-Path $PSScriptRoot "golden-baseline.ps1") -Compare `
            -BuildId $GoldenBuildId -SkipBuild
    }
}

Write-Output ""
Write-Output "run-checks passed: $($steps -join ' -> ')"
