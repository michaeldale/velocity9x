# Assemble the downloadable release folder: releases\<version>.
#
# Everything else in scripts\ writes to build\, which is git-ignored because it
# is a scratch area rebuilt on every run. That is the right place for a working
# tree and the wrong place for something a stranger with an S3 card is meant to
# download. This script takes what those scripts already produced and turns it
# into one committed folder per version: one zip per family, one for the survey,
# a checksum list, and an index that says which zip goes with which card.
#
# It does not compile anything. Run the builders first - build-all-packages.ps1
# and build-vga-survey.ps1 - so that what is published is exactly what was
# audited, then run this. The version and build id are read back out of
# build\packages.json rather than recomputed, and the version is checked against
# include\velocity9x\build.h so a stale build\ cannot be published as a new
# release.
[CmdletBinding()]
param(
    # Overwrite an existing releases\<version> folder. Off by default: a
    # published version is a fixed thing, and quietly replacing one is how the
    # checksums people were given stop matching what they can download.
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")

$productVersion = Get-V9xProductVersion -RepoRoot $repoRoot

$indexPath = Join-Path $repoRoot "build\packages.json"
if (-not (Test-Path -LiteralPath $indexPath)) {
    throw ("No build\packages.json. Run scripts\build-all-packages.ps1 first.")
}
$index = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json

if ($index.Version -ne $productVersion) {
    throw ("build\packages.json was produced for $($index.Version) but " +
           "include\velocity9x\build.h now says $productVersion. Rebuild the " +
           "packages before publishing.")
}

$buildId = $index.BuildId
if ($buildId -match '-dirty$') {
    throw ("build\packages.json was built from a dirty tree ($buildId). A " +
           "release has to be attributable to a commit: commit or stash, " +
           "rebuild, then publish.")
}

$surveyDir = Join-Path $repoRoot "build\vga-survey"
if (-not (Test-Path -LiteralPath $surveyDir)) {
    throw "No build\vga-survey. Run scripts\build-vga-survey.ps1 first."
}

$releaseDir = Join-Path $repoRoot "releases\$productVersion"
if (Test-Path -LiteralPath $releaseDir) {
    if (-not $Force) {
        throw "releases\$productVersion already exists. Pass -Force to replace it."
    }
    Remove-Item -LiteralPath $releaseDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

# One zip per family rather than loose files. A zip is a single click on a
# forge's file view; a folder of twenty-two files is a clone. It also keeps the
# .EXE, .DRV, .VXD and .DLL out of the working tree, where the repository's
# ignore rules would otherwise have to carve out an exception for them.
function New-V9xReleaseZip {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDir,
        [Parameter(Mandatory = $true)][string]$ZipPath
    )

    $items = @(Get-ChildItem -LiteralPath $SourceDir -File | Sort-Object Name)
    if ($items.Count -eq 0) {
        throw "Nothing to publish in $SourceDir."
    }
    Compress-Archive -LiteralPath @($items | ForEach-Object { $_.FullName }) `
        -DestinationPath $ZipPath -CompressionLevel Optimal
    return $items.Count
}

$published = @()

foreach ($package in $index.Packages) {
    $sourceDir = Join-Path $repoRoot "build\$($package.Directory)"
    if (-not (Test-Path -LiteralPath $sourceDir)) {
        throw "Family $($package.Family) names build\$($package.Directory), which does not exist."
    }

    $zipName = "velocity9x-$productVersion-$($package.Family).zip"
    $zipPath = Join-Path $releaseDir $zipName
    $fileCount = New-V9xReleaseZip -SourceDir $sourceDir -ZipPath $zipPath

    # The index records what was built; the folder on disk is what is being
    # zipped. If they disagree, build\ has been touched since the build and the
    # checksums about to be written would describe neither.
    if ($fileCount -ne $package.Files.Count) {
        throw ("build\$($package.Directory) holds $fileCount files but " +
               "packages.json recorded $($package.Files.Count). Rebuild.")
    }

    $published += [pscustomobject]@{
        Name = $zipName
        Title = $package.DisplayName
        HardwareIds = @($package.HardwareIds)
        Files = $fileCount
        Manifest = (Join-Path $sourceDir "MANIFEST.TXT")
    }
    Write-Output "Published $zipName ($fileCount files)"
}

$surveyZipName = "velocity9x-survey-$productVersion.zip"
$surveyFiles = New-V9xReleaseZip -SourceDir $surveyDir `
    -ZipPath (Join-Path $releaseDir $surveyZipName)
Write-Output "Published $surveyZipName ($surveyFiles files)"

# Checksums over the zips, not over their contents. Each package already ships
# its own SHA256.TXT covering the files inside it; what this list answers is the
# separate question of whether the download itself arrived intact.
$hashLines = Get-ChildItem -LiteralPath $releaseDir -File -Filter *.zip |
    Sort-Object Name |
    ForEach-Object {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
        "$hash  $($_.Name)"
    }
Set-Content -LiteralPath (Join-Path $releaseDir "SHA256SUMS.txt") `
    -Encoding Ascii -Value $hashLines

# The index is generated, not written by hand, so that the hardware ids and the
# tested-status line in it are the ones the build actually stamped into each
# package rather than a copy that drifts a version behind.
function Get-V9xManifestField {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$Field
    )

    $line = @(Get-Content -LiteralPath $ManifestPath |
        Where-Object { $_ -like "${Field}: *" })
    if ($line.Count -eq 0) {
        return ""
    }
    return $line[0].Substring($Field.Length + 2)
}

$readme = @()
$readme += "# Velocity9x $productVersion"
$readme += ""
$readme += "Built from commit ``$buildId``: the output of"
$readme += "``scripts\build-all-packages.ps1`` and ``scripts\build-vga-survey.ps1``,"
$readme += "zipped for download."
$readme += ""
$readme += "Windows 98SE. Each zip carries its own instructions and its own recovery"
$readme += "notes - start with the ``.TXT`` files at the top level of the archive, and"
$readme += "read them before you install anything. This is a from-scratch display"
$readme += "driver: a bad install leaves the machine at a black screen until you"
$readme += "recover it."
$readme += ""
$readme += "## Which zip"
$readme += ""
$readme += "| Download | Card | Hardware ID | Tested |"
$readme += "| --- | --- | --- | --- |"
foreach ($entry in $published) {
    $ids = ($entry.HardwareIds -join "<br>")
    $status = Get-V9xManifestField -ManifestPath $entry.Manifest -Field "Status"
    $readme += "| [``$($entry.Name)``]($($entry.Name)) | $($entry.Title) | ``$ids`` | $status |"
}
$readme += ""
$readme += "If your card is not listed, none of these will drive it. Run the survey"
$readme += "below and send the report in; that is what a new family is built from."
$readme += ""
foreach ($entry in $published) {
    $modes = Get-V9xManifestField -ManifestPath $entry.Manifest -Field "Modes"
    if ($modes) {
        $readme += "- **$($entry.Title)** modes: $modes"
    }
}
$readme += ""
$readme += "## Hardware survey"
$readme += ""
$readme += "[``$surveyZipName``]($surveyZipName) - a real-mode DOS program that reads"
$readme += "the PCI identifiers, video BIOS, advertised modes, monitor EDID and VGA"
$readme += "registers of whatever card is in the machine, and writes one report file."
$readme += "It sets no video mode, installs nothing, and writes to no register."
$readme += "Run it on an unsupported card and send the report; ``README.TXT`` inside"
$readme += "the zip has the instructions."
$readme += ""
$readme += "## Checksums"
$readme += ""
$readme += "``SHA256SUMS.txt`` covers the zips in this folder. Each package also ships"
$readme += "its own ``SHA256.TXT`` covering the files inside it."
$readme += ""

Set-Content -LiteralPath (Join-Path $releaseDir "README.md") `
    -Encoding Ascii -Value $readme

# The top-level index is rewritten from the folders that exist rather than
# appended to, so removing a published version cannot leave a link to it behind.
$versionDirs = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "releases") -Directory |
    Sort-Object { [version]$_.Name } -Descending)

$indexLines = @()
$indexLines += "# Velocity9x releases"
$indexLines += ""
$indexLines += "Built packages, one folder per version. The newest is at the top."
$indexLines += "Open a version's ``README.md`` to see which zip matches which card."
$indexLines += ""
foreach ($dir in $versionDirs) {
    $zipCount = @(Get-ChildItem -LiteralPath $dir.FullName -File -Filter *.zip).Count
    $indexLines += "- [$($dir.Name)]($($dir.Name)/README.md) - $zipCount downloads"
}
$indexLines += ""
$indexLines += "These are built, not hand-assembled: ``scripts\build-all-packages.ps1``,"
$indexLines += "then ``scripts\build-vga-survey.ps1``, then ``scripts\build-release.ps1``."
$indexLines += ""

Set-Content -LiteralPath (Join-Path $repoRoot "releases\README.md") `
    -Encoding Ascii -Value $indexLines

Write-Output "Wrote releases\$productVersion (version $productVersion, build $buildId)"
