[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\host"

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "local"
}

$watcomRoot = $env:WATCOM
if (-not $watcomRoot -and (Test-Path -LiteralPath "C:\WATCOM")) {
    $watcomRoot = "C:\WATCOM"
}
if ($watcomRoot) {
    $env:WATCOM = $watcomRoot
    $env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
    $env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
}

$compiler = Get-Command "wcl386.exe" -ErrorAction SilentlyContinue
if (-not $compiler -and $watcomRoot) {
    $candidates = @(
        (Join-Path $watcomRoot "binnt64\wcl386.exe"),
        (Join-Path $watcomRoot "binnt\wcl386.exe")
    )
    $compiler = $candidates | Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
}
if (-not $compiler) {
    throw "Open Watcom wcl386.exe was not found. Set WATCOM or install it at C:\WATCOM."
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

# The host tests assert against a matrix generated from the family
# manifests; see scripts\lib\family-matrix.ps1 for why.
. (Join-Path $PSScriptRoot "lib\family-matrix.ps1")
$null = Write-V9xFamilyMatrixHeader -RepoRoot $repoRoot -OutputDir $outputDir
# And the OpenGL dispatch table, generated from its manifest for the same
# reason; see scripts\lib\gl-dispatch.ps1.
. (Join-Path $PSScriptRoot "lib\gl-dispatch.ps1")
$null = Write-V9xGlDispatchHeader -RepoRoot $repoRoot -OutputDir $outputDir

# Both compilers consume one portable source list, with the Watcom-only x87
# converter and its test selected explicitly by the shared helper.
. (Join-Path $PSScriptRoot "lib\host-sources.ps1")
$sourceNames = @(Get-V9xHostSourceNames -RepoRoot $repoRoot -Compiler Watcom)
$executable = Join-Path $outputDir "v9x-host-tests.exe"
$sources = @($sourceNames | ForEach-Object { Join-Path $repoRoot $_ })
$arguments = @(
    "-bt=nt",
    "-zq",
    # -wx is the warning level; -we is what makes a warning fail the build.
    # Both are set on the driver and HAL compiles, so the host build matches.
    "-wx",
    "-we",
    "-i=$(Join-Path $repoRoot 'include')",
    # The generated family matrix lives beside the test executable.
    "-i=$outputDir",
    "-dV9X_BUILD_ID=`"$BuildId`"",
    "-fe=$executable"
) + $sources

Push-Location $outputDir
try {
    & $compiler @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom compilation failed with exit code $LASTEXITCODE."
    }
    & $executable
    if ($LASTEXITCODE -ne 0) {
        throw "Host tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
