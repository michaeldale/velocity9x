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

# cl.exe launches link.exe by name. A host that also has Open Watcom installed
# can otherwise compile with MSVC and then accidentally hand the objects to
# Watcom's linker, depending on the caller's PATH ordering. Keep the compiler
# and linker from the same Visual C++ tools directory as an inseparable pair.
$msvcBin = Split-Path -Parent $cl.Source
$msvcLink = Join-Path $msvcBin "link.exe"
if (-not (Test-Path -LiteralPath $msvcLink)) {
    throw "The MSVC linker was not found beside cl.exe: $msvcLink"
}
$env:Path = $msvcBin + ";" + $env:Path

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

# Generated into this pass's own output directory so it never compiles against
# a header the Watcom build happened to leave behind.
. (Join-Path $PSScriptRoot "lib\family-matrix.ps1")
$null = Write-V9xFamilyMatrixHeader -RepoRoot $repoRoot -OutputDir $outputDir

# Both compilers consume one portable source list, with the Watcom-only x87
# converter and its test selected explicitly by the shared helper.
. (Join-Path $PSScriptRoot "lib\host-sources.ps1")
$sourceNames = @(Get-V9xHostSourceNames -RepoRoot $repoRoot -Compiler MSVC)
$executable = Join-Path $outputDir "v9x-host-tests.exe"
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
