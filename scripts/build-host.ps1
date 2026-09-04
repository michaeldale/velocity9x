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

# The chipset policy backends come from the manifests' Backend.Sources rather
# than a list here, so a new family joins the host build by existing. The
# registry table those backends satisfy is generated from the same manifests.
. (Join-Path $PSScriptRoot "lib\family.ps1")
$backendSourceNames = @(Get-V9xFamilies -RepoRoot $repoRoot |
    ForEach-Object { @($_.Backend.Sources) } | Sort-Object -Unique)

$executable = Join-Path $outputDir "v9x-host-tests.exe"
$sourceNames = @(
    "src\common\build.c",
    "src\common\backend_registry.c",
    "src\common\mode.c",
    "src\common\log.c",
    "src\common\resources.c",
    "src\common\vbe_parse.c",
    "src\common\vbe_modes.c",
    "src\common\vbe_cache.c",
    "src\common\edid.c",
    "src\common\mtrr.c",
    "src\common\d3dmode.c",
    "src\common\vbe_crtc.c",
    "src\common\donewait.c",
    # A leaf translation unit that happens to live under src\display32\d3d: it
    # includes nothing from the DDHAL side, so the host build can reach it
    # without dragging in MMIO or the #pragma aux converters. It stays on the
    # engine side of the D3D split rather than moving to src\common, because a
    # ViRGE depth encoding is chip vocabulary - see the header comment.
    "src\display32\d3d\d3d_zfixed.c",
    # The software rasterizer's arithmetic, on the same terms: it includes
    # nothing but velocity9x\types.h, holds no state and takes its render
    # target as a pointer, so the host can hand it an array and count pixels.
    "src\display32\d3d\d3d_raster.c"
) + $backendSourceNames + @(
    "src\display16\display_component.c",
    "src\minivdd32\minivdd_component.c",
    "tests\host\test_family_matrix.c",
    "tests\host\test_hw16_modes.c",
    "tests\host\test_vbe_parse.c",
    "tests\host\test_vbe_modes.c",
    "tests\host\test_vbe_cache.c",
    "tests\host\test_edid.c",
    "tests\host\test_mtrr.c",
    "tests\host\test_d3dmode.c",
    "tests\host\test_vbe_crtc.c",
    "tests\host\test_d3d_zfixed.c",
    "tests\host\test_d3d_raster.c",
    "tests\host\test_donewait.c",
    "tests\host\test_main.c"
)
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
