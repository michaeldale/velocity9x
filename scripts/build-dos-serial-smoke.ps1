[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\dos-diag"

. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "lib\diag-tool.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "dos-diag-local"
}

$source = Join-Path $repoRoot "tools\diag\serial_smoke.c"
$executable = Join-Path $outputDir "v9xser.exe"

Invoke-V9xDiagToolBuild -Target Dos -OutputDir $outputDir `
    -Source $source -Executable $executable `
    -CompileArguments @("-bt=dos", "-ms", "-zq", "-wx",
        "-dV9X_BUILD_ID=`"$BuildId`"", "-fe=$executable", $source) `
    -ToolDescription "DOS serial smoke utility" | Out-Null

# The per-tool contract: an MZ executable that carries its build identifier.
$bytes = [System.IO.File]::ReadAllBytes($executable)
if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "The DOS serial smoke output is not an MZ executable."
}
if (-not [System.Text.Encoding]::ASCII.GetString($bytes).Contains($BuildId)) {
    throw "The DOS serial smoke output does not contain the build identifier."
}

Write-Output "Built DOS COM1 smoke utility: $executable"
