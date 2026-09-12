[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\intel-survey"

. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "lib\diag-tool.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "intel-survey-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "Invalid BuildId"
}

$source = Join-Path $repoRoot "tools\diag\intel_survey_dos.c"
$exe = Join-Path $outputDir "v9xintl.exe"

Invoke-V9xDiagToolBuild -Target Dos -OutputDir $outputDir `
    -Source $source -Executable $exe `
    -CompileArguments @("-bt=dos", "-ms", "-zq", "-wx",
        "-dV9X_BUILD_ID=`"$BuildId`"", "-fe=$exe", $source) `
    -ToolDescription "DOS Intel GMA survey" | Out-Null

# The per-tool contract: an MZ executable that carries its build identifier
# and the markers that identify it as the query-only survey.
$bytes = [IO.File]::ReadAllBytes($exe)
if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "The Intel GMA survey is not an MZ executable."
}
$text = [Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @($BuildId, "Velocity9x Intel GMA survey", "query-only")) {
    if (-not $text.Contains($marker)) {
        throw "Intel GMA survey lacks marker $marker"
    }
}

Write-Output "Built no-mode-change DOS Intel GMA survey: $exe"
