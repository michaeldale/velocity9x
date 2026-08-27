# Regenerates src\common\backend_registry_table.inc from the family manifests.
#
# Run after adding a family or changing any chip's PCI id, then commit the
# result. check-tree.ps1 regenerates the same content and fails when the
# checked-in file differs, so forgetting this step cannot ship.
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "lib\backend-registry.ps1")

$target = Join-Path $repoRoot "src\common\backend_registry_table.inc"
$null = Write-V9xBackendRegistryTable -RepoRoot $repoRoot -OutputPath $target
Write-Output "Regenerated $target."
