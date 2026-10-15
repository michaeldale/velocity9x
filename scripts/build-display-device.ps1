[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\display-device"

. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "lib\diag-tool.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "display-device-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "Invalid BuildId"
}

$source = Join-Path $repoRoot "tools\diag\display_device_dos.c"
$exe = Join-Path $outputDir "v9xddev.exe"

Invoke-V9xDiagToolBuild -Target Dos -OutputDir $outputDir `
    -Source $source -Executable $exe `
    -CompileArguments @("-bt=dos", "-ms", "-zq", "-wx",
        "-i=$(Join-Path $repoRoot 'include')",
        "-dV9X_BUILD_ID=`"$BuildId`"", "-fe=$exe", $source) `
    -ToolDescription "DOS display-device switch" | Out-Null

# The per-tool contract: an MZ executable that carries its build identifier
# and its tool name, so a log line can be traced to the binary that wrote it.
$bytes = [IO.File]::ReadAllBytes($exe)
if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "The display-device switch is not an MZ executable."
}
$text = [Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @($BuildId, "Velocity9x display-device switch")) {
    if (-not $text.Contains($marker)) {
        throw "Display-device switch lacks marker $marker"
    }
}

Write-Output "Built DOS display-device switch: $exe"
