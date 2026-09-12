[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\vbe-inventory"

. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "lib\diag-tool.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "vbe-inventory-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "Invalid BuildId"
}

$source = Join-Path $repoRoot "tools\diag\vbe_inventory_dos.c"
$exe = Join-Path $outputDir "v9xvbe.exe"

Invoke-V9xDiagToolBuild -Target Dos -OutputDir $outputDir `
    -Source $source -Executable $exe `
    -CompileArguments @("-bt=dos", "-ms", "-zq", "-wx",
        "-i=$(Join-Path $repoRoot 'include')",
        "-dV9X_BUILD_ID=`"$BuildId`"", "-fe=$exe", $source) `
    -ToolDescription "DOS VBE inventory" | Out-Null

# The per-tool contract: an MZ executable that carries its build identifier
# and the markers that identify it as the query-only inventory.
$bytes = [IO.File]::ReadAllBytes($exe)
if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "The VBE inventory is not an MZ executable."
}
$text = [Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @($BuildId, "Velocity9x VBE inventory", "query-only")) {
    if (-not $text.Contains($marker)) {
        throw "VBE inventory lacks marker $marker"
    }
}

Write-Output "Built no-mode-change DOS VBE inventory: $exe"
