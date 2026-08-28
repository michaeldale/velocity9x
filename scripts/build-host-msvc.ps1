[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\host-msvc"

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "local"
}

$cl = Get-Command "cl.exe" -ErrorAction SilentlyContinue
if (-not $cl) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsRoot = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($vsRoot) {
            $devCmd = Join-Path $vsRoot "Common7\Tools\VsDevCmd.bat"
            $envDump = cmd /c "`"$devCmd`" -arch=x64 -no_logo && set"
            $developerPath = $null
            foreach ($line in $envDump) {
                if ($line -match '^([^=]+)=(.*)$') {
                    if ($Matches[1] -ieq "Path") {
                        if ($Matches[2] -match '\\VC\\Tools\\MSVC\\') {
                            $developerPath = $Matches[2]
                        }
                    }
                    else {
                        Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
                    }
                }
            }
            if ($developerPath) {
                $env:Path = $developerPath
            }
            $cl = Get-Command "cl.exe" -ErrorAction SilentlyContinue
            if (-not $cl -and $env:VCToolsInstallDir) {
                $clPath = Join-Path $env:VCToolsInstallDir `
                    "bin\Hostx64\x64\cl.exe"
                if (Test-Path -LiteralPath $clPath) {
                    $cl = Get-Command $clPath
                }
            }
        }
    }
}
if (-not $cl) {
    throw "MSVC cl.exe was not found. Run from a Developer PowerShell or install the VS Build Tools."
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

# Generated into this pass's own output directory so it never compiles against
# a header the Watcom build happened to leave behind.
. (Join-Path $PSScriptRoot "lib\family-matrix.ps1")
$null = Write-V9xFamilyMatrixHeader -RepoRoot $repoRoot -OutputDir $outputDir

# The chipset policy backends come from the manifests' Backend.Sources, the
# same derivation build-host.ps1 uses - which is also what keeps the two
# passes' source sets from drifting apart in that section, the failure mode
# the comment below records.
. (Join-Path $PSScriptRoot "lib\family.ps1")
$backendSourceNames = @(Get-V9xFamilies -RepoRoot $repoRoot |
    ForEach-Object { @($_.Backend.Sources) } | Sort-Object -Unique)

$executable = Join-Path $outputDir "v9x-host-tests.exe"
# Must stay the same set build-host.ps1 compiles. It had drifted: the clock,
# memory, registry and Matrox modules were missing, so this pass had stopped
# linking - every run failed at the link step and never ran a test.
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
    "src\common\vbe_crtc.c"
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
    "tests\host\test_vbe_crtc.c",
    "tests\host\test_main.c"
)
$sources = @($sourceNames | ForEach-Object { Join-Path $repoRoot $_ })
$arguments = @(
    "/nologo",
    "/W4",
    "/WX",
    "/I$(Join-Path $repoRoot 'include')",
    "/I$outputDir",
    "/DV9X_BUILD_ID=\`"$BuildId\`"",
    "/Fe$executable"
) + $sources

Push-Location $outputDir
try {
    & $cl.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "MSVC compilation failed with exit code $LASTEXITCODE."
    }
    & $executable
    if ($LASTEXITCODE -ne 0) {
        throw "Host tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
