# Capture and compare the byte-level golden baseline for the multi-chip
# restructure (docs\plans\multi-chip-restructure.md phases 1-7).
#
# Through phase 7 every restructure step must leave the shipped ViRGE, Trio64
# and Matrox images byte-identical when built with a pinned -BuildId. This
# script freezes that state and diffs against it. The archive deliberately
# lives outside build\, because build\ is what the restructure rewrites.
[CmdletBinding(DefaultParameterSetName = "Compare")]
param(
    [Parameter(ParameterSetName = "Capture", Mandatory = $true)]
    [switch]$Capture,
    [Parameter(ParameterSetName = "Compare")]
    [switch]$Compare,
    [string]$BuildId = "golden-compare",
    # Default archive root sits beside the repository, not inside it: the tree
    # is not a build artefact and must survive a build\ wipe.
    [string]$ArchiveRoot,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $ArchiveRoot) {
    $ArchiveRoot = Join-Path (Split-Path -Parent $repoRoot) "velocity9x-golden"
}
$archiveDir = Join-Path $ArchiveRoot $BuildId
$manifestPath = Join-Path $archiveDir "golden.txt"

# Trees whose contents are hashed, and the map files whose segment sizes are
# recorded separately because their absolute paths and timestamps differ per
# run.
#
# Phase 8 merged the two S3 packages into one, so this is no longer a
# byte-for-byte gate against the pre-restructure images: build\win98se-s3
# contains both chips and can reproduce neither win98se-active nor
# win98se-trio64. What it still does, and what it is now for, is catch an
# unintended change between two builds of the current tree - and the map sizes
# remain the code-growth budget.
$trackedTrees = @(
    "build\win98se-s3",
    "build\floppy",
    "build\matrox-candidate"
)
$trackedMaps = @(
    "build\win16-ddi-s3\v9xdisp.map",
    "build\win16-ddi-mga2\v9xdisp.map"
)

# Win32 PE images embed the link timestamp, so two identical builds hash
# differently. The link second is stamped into the COFF header, the export and
# import directories, every resource directory node and the debug directory -
# and those copies are not all equal, because a link that straddles a second
# boundary writes two different values. So each field is zeroed where it lives
# rather than by pattern-matching the COFF value. NE images (the Win16 driver)
# and VxDs carry no such field and are hashed as-is.
$sha256 = [Security.Cryptography.SHA256]::Create()

function Get-NormalizedFileHash {
    param([Parameter(Mandatory = $true)][string]$Path)

    $bytes = [IO.File]::ReadAllBytes($Path)
    Clear-PeTimestamp -Bytes $bytes
    -join ($sha256.ComputeHash($bytes) | ForEach-Object { "{0:X2}" -f $_ })
}

function Clear-PeTimestamp {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    if ($Bytes.Length -lt 0x40 -or $Bytes[0] -ne 0x4d -or $Bytes[1] -ne 0x5a) {
        return
    }
    $pe = [BitConverter]::ToInt32($Bytes, 0x3c)
    if ($pe -le 0 -or $pe + 0x18 -ge $Bytes.Length -or
        $Bytes[$pe] -ne 0x50 -or $Bytes[$pe + 1] -ne 0x45 -or
        $Bytes[$pe + 2] -ne 0 -or $Bytes[$pe + 3] -ne 0) {
        return
    }

    $zero = {
        param([int]$Offset)
        if ($Offset -ge 0 -and $Offset + 4 -le $Bytes.Length) {
            $Bytes[$Offset] = 0; $Bytes[$Offset + 1] = 0
            $Bytes[$Offset + 2] = 0; $Bytes[$Offset + 3] = 0
        }
    }

    $sectionCount = [BitConverter]::ToUInt16($Bytes, $pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($Bytes, $pe + 20)
    $optional = $pe + 24
    $sections = $optional + $optionalSize
    & $zero ($pe + 8)             # COFF TimeDateStamp
    & $zero ($optional + 64)      # OptionalHeader.CheckSum

    $directoryCount = [BitConverter]::ToInt32($Bytes, $optional + 92)
    $directories = $optional + 96

    function Convert-RvaToOffset {
        param([int]$Rva)
        for ($index = 0; $index -lt $sectionCount; ++$index) {
            $header = $sections + ($index * 40)
            if ($header + 40 -gt $Bytes.Length) { return -1 }
            $virtual = [BitConverter]::ToInt32($Bytes, $header + 12)
            $rawSize = [BitConverter]::ToInt32($Bytes, $header + 16)
            $rawPointer = [BitConverter]::ToInt32($Bytes, $header + 20)
            $virtualSize = [BitConverter]::ToInt32($Bytes, $header + 8)
            $span = [Math]::Max($rawSize, $virtualSize)
            if ($Rva -ge $virtual -and $Rva -lt $virtual + $span) {
                return $rawPointer + ($Rva - $virtual)
            }
        }
        return -1
    }

    function Get-DirectoryOffset {
        param([int]$Index)
        if ($Index -ge $directoryCount) { return -1 }
        $entry = $directories + ($Index * 8)
        if ($entry + 8 -gt $Bytes.Length) { return -1 }
        $rva = [BitConverter]::ToInt32($Bytes, $entry)
        $size = [BitConverter]::ToInt32($Bytes, $entry + 4)
        if ($rva -eq 0 -or $size -eq 0) { return -1 }
        Convert-RvaToOffset -Rva $rva
    }

    $exportOffset = Get-DirectoryOffset -Index 0
    if ($exportOffset -ge 0) { & $zero ($exportOffset + 4) }

    $importOffset = Get-DirectoryOffset -Index 1
    if ($importOffset -ge 0) {
        for ($descriptor = $importOffset;
             $descriptor + 20 -le $Bytes.Length;
             $descriptor += 20) {
            $empty = $true
            for ($byte = 0; $byte -lt 20; ++$byte) {
                if ($Bytes[$descriptor + $byte] -ne 0) { $empty = $false; break }
            }
            if ($empty) { break }
            & $zero ($descriptor + 4)
        }
    }

    # Resource directory nodes each carry a TimeDateStamp; walk the tree.
    $resourceOffset = Get-DirectoryOffset -Index 2
    if ($resourceOffset -ge 0) {
        $pending = New-Object 'System.Collections.Generic.Queue[int]'
        $visited = New-Object 'System.Collections.Generic.HashSet[int]'
        $pending.Enqueue(0)
        while ($pending.Count -gt 0) {
            $relative = $pending.Dequeue()
            if (-not $visited.Add($relative)) { continue }
            $node = $resourceOffset + $relative
            if ($node + 16 -gt $Bytes.Length) { continue }
            & $zero ($node + 4)
            $entryCount = [BitConverter]::ToUInt16($Bytes, $node + 12) +
                          [BitConverter]::ToUInt16($Bytes, $node + 14)
            for ($entry = 0; $entry -lt $entryCount; ++$entry) {
                $entryOffset = $node + 16 + ($entry * 8)
                if ($entryOffset + 8 -gt $Bytes.Length) { break }
                $target = [BitConverter]::ToUInt32($Bytes, $entryOffset + 4)
                if (($target -band 0x80000000) -ne 0) {
                    $pending.Enqueue([int]($target -band 0x7fffffff))
                }
            }
        }
    }

    $debugEntry = Get-DirectoryOffset -Index 6
    if ($debugEntry -ge 0) {
        $debugSize = [BitConverter]::ToInt32($Bytes, $directories + (6 * 8) + 4)
        for ($offset = 0; $offset + 28 -le $debugSize; $offset += 28) {
            & $zero ($debugEntry + $offset + 4)
        }
    }
}

function Get-TreeHashLines {
    $lines = @()
    foreach ($tree in $trackedTrees) {
        $treePath = Join-Path $repoRoot $tree
        if (-not (Test-Path -LiteralPath $treePath)) {
            throw "Golden tree $tree is missing. Build it first."
        }
        $prefixLength = $treePath.Length + 1
        $files = @(Get-ChildItem -LiteralPath $treePath -Recurse -File |
            Sort-Object FullName)
        foreach ($file in $files) {
            # SHA256.TXT is derived from the un-normalised hashes of its own
            # package, so it moves with every link timestamp. The files it
            # covers are all tracked individually here instead.
            if ($file.Name -eq "SHA256.TXT") {
                continue
            }
            $relative = $file.FullName.Substring($prefixLength)
            $hash = Get-NormalizedFileHash -Path $file.FullName
            $lines += "{0}  {1}/{2}" -f $hash, $tree.Replace('\', '/'), $relative.Replace('\', '/')
        }
    }
    $lines
}

# Segment sizes are the phase gate for 16-bit code growth (2 KiB per step).
# The map's own header carries absolute paths and a timestamp, so only the
# segment table is extracted.
function Get-MapSegmentLines {
    $lines = @()
    foreach ($map in $trackedMaps) {
        $mapPath = Join-Path $repoRoot $map
        if (-not (Test-Path -LiteralPath $mapPath)) {
            throw "Golden map $map is missing. Build it first."
        }
        $text = Get-Content -LiteralPath $mapPath -Raw
        $segments = [regex]::Matches(
            $text,
            '(?m)^(?<name>\S+)\s+(?<class>CODE|DATA|BSS)\s+(?<group>\S+)\s+' +
            '(?<addr>[0-9A-Fa-f]{4}:[0-9A-Fa-f]{4})\s+(?<size>[0-9A-Fa-f]{8})\s*$')
        if ($segments.Count -eq 0) {
            throw "Could not parse the segment table in $map."
        }
        foreach ($segment in $segments) {
            $lines += "{0}  segment {1} {2} size {3}" -f $map.Replace('\', '/'),
                $segment.Groups['name'].Value, $segment.Groups['class'].Value,
                $segment.Groups['size'].Value
        }
        $group = [regex]::Match(
            $text,
            '(?m)^DGROUP\s+(?<addr>[0-9A-Fa-f]{4}:[0-9A-Fa-f]{4})\s+(?<size>[0-9A-Fa-f]{8})\s*$')
        if ($group.Success) {
            $lines += "{0}  group DGROUP size {1}" -f $map.Replace('\', '/'),
                $group.Groups['size'].Value
        }
    }
    $lines
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build-floppy-package.ps1") -BuildId $BuildId |
        Out-Null
    & (Join-Path $PSScriptRoot "build-matrox-candidate.ps1") -BuildId $BuildId |
        Out-Null
}

$current = @("# Velocity9x golden baseline", "# BuildId: $BuildId") +
    (Get-TreeHashLines) + (Get-MapSegmentLines)

if ($Capture) {
    New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
    Set-Content -LiteralPath $manifestPath -Value $current -Encoding Ascii
    foreach ($tree in $trackedTrees) {
        $destination = Join-Path $archiveDir (Split-Path -Leaf $tree)
        if (Test-Path -LiteralPath $destination) {
            Remove-Item -LiteralPath $destination -Recurse -Force
        }
        Copy-Item -LiteralPath (Join-Path $repoRoot $tree) `
            -Destination $destination -Recurse -Force
    }
    Write-Output "Captured golden baseline: $manifestPath"
    Write-Output ("  {0} tracked entries" -f ($current.Count - 2))
    return
}

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "No golden baseline at $manifestPath. Run with -Capture first."
}
$baseline = @(Get-Content -LiteralPath $manifestPath)
$differences = @(Compare-Object -ReferenceObject $baseline -DifferenceObject $current)
if ($differences.Count -ne 0) {
    $differences | ForEach-Object {
        Write-Output ("{0} {1}" -f $_.SideIndicator, $_.InputObject)
    }
    throw "Golden compare failed: $($differences.Count) differing entries."
}
Write-Output "Golden compare passed ($($baseline.Count - 2) entries, BuildId $BuildId)."
