# Build every family package declared under packaging\families and record what
# was produced in build\packages.json.
#
# The index is what tells a later step - a release, a VM run, a support request
# - exactly which family, version, build and file hashes it is looking at.
[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = "C:\98DDK",
    # Restrict the run to named families. Default is every declared family.
    [string[]]$Family,
    [switch]$SkipFloppy
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "lib\family.ps1")

$ProductVersion = Get-V9xProductVersion -RepoRoot $repoRoot
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "packages-local"
}

$families = @(Get-V9xFamilies -RepoRoot $repoRoot)
if ($Family) {
    $unknown = @($Family | Where-Object { $_ -notin @($families | ForEach-Object { $_.Id }) })
    if ($unknown.Count -ne 0) {
        throw "Unknown family/families: $($unknown -join ', ')."
    }
    $families = @($families | Where-Object { $_.Id -in $Family })
}

$entries = @()
# Not $family: the [string[]]$Family parameter is the same name to PowerShell,
# and assigning a manifest hashtable to it silently coerces it to strings.
foreach ($manifest in $families) {
    $directoryName = Split-Path -Leaf $manifest.Build.PackageOutput
    $outputDir = Join-Path $repoRoot "build\$directoryName"

    if ($manifest.Inf.Generate -eq $false) {
        # Families that install by guarded file replacement have their own
        # packaging script; there is no INF package to build here.
        $script = Join-Path $PSScriptRoot "build-matrox-candidate.ps1"
        if (-not (Test-Path -LiteralPath $script)) {
            throw "Family $($manifest.Id) has no INF and no dedicated packaging script."
        }
        & $script -BuildId $BuildId -DdkRoot $DdkRoot | Write-Verbose
    } else {
        & (Join-Path $PSScriptRoot "build-active-package.ps1") `
            -BuildId $BuildId -DdkRoot $DdkRoot -Family $manifest.Id | Write-Verbose
    }

    $files = @(Get-ChildItem -LiteralPath $outputDir -File | Sort-Object Name |
        ForEach-Object {
            [pscustomobject]@{
                Name = $_.Name
                Bytes = $_.Length
                Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
            }
        })
    $entries += [pscustomobject]@{
        Family = $manifest.Id
        DisplayName = $manifest.DisplayName
        HardwareIds = @(Get-V9xFamilyHardwareIds -Family $manifest)
        Directory = $directoryName
        Files = $files
    }
    Write-Output "Built family $($manifest.Id): $outputDir ($($files.Count) files)"
}

if (-not $SkipFloppy -and @($families | Where-Object { $_.Floppy.Include }).Count -ne 0) {
    & (Join-Path $PSScriptRoot "build-floppy-package.ps1") `
        -BuildId $BuildId -DdkRoot $DdkRoot -SkipBuild | Write-Verbose
    Write-Output "Assembled floppy transfer folder: build\floppy"
}

$index = [pscustomobject]@{
    Version = $ProductVersion
    BuildId = $BuildId
    Packages = $entries
}
$indexPath = Join-Path $repoRoot "build\packages.json"
$index | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $indexPath -Encoding Ascii
Write-Output "Wrote package index: $indexPath"
