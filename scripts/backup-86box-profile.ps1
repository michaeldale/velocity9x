# Cold backup of an 86Box VM profile.
#
# Point -ProfilePath at the 86Box profile directory to back up, or set
# V9X_86BOX_PROFILE. Backups land beside the profile unless -BackupRoot says
# otherwise. There is no built-in default profile: this script copies whatever
# it is given, so it must be told explicitly rather than guessing.
[CmdletBinding()]
param(
    [string]$ProfilePath = $env:V9X_86BOX_PROFILE,
    [string]$BackupRoot
)

$ErrorActionPreference = "Stop"

if (-not $ProfilePath) {
    throw "Specify -ProfilePath (or set V9X_86BOX_PROFILE) to the 86Box profile directory to back up."
}
$profileItem = Get-Item -LiteralPath $ProfilePath -ErrorAction Stop
if (-not $profileItem.PSIsContainer) {
    throw "The VM profile path is not a directory: $ProfilePath"
}
$resolvedProfile = $profileItem.FullName.TrimEnd('\')
# Guard against being aimed at an arbitrary directory: a real 86Box profile
# always carries its own configuration file next to the disk images.
if (-not (Test-Path -LiteralPath (Join-Path $resolvedProfile "86box.cfg"))) {
    throw "No 86box.cfg in $resolvedProfile - that is not an 86Box VM profile directory."
}
if (-not $BackupRoot) {
    $BackupRoot = Join-Path (Split-Path -Parent $resolvedProfile) "Velocity9x Backups"
}

$running86Box = @(Get-Process -Name "86Box" -ErrorAction SilentlyContinue)
if ($running86Box.Count -ne 0) {
    throw "86Box is still running. Fully close the VM and manager before a cold backup."
}

$configPath = Join-Path $resolvedProfile "86box.cfg"
$configLines = Get-Content -LiteralPath $configPath -ErrorAction Stop
$diskNames = @($configLines | ForEach-Object {
    if ($_ -match '^hdd_[0-9]+_fn\s*=\s*(.+?)\s*$') {
        $matches[1]
    }
} | Where-Object { $_ } | Sort-Object -Unique)
if ($diskNames.Count -eq 0) {
    throw "The active 86box.cfg does not reference a hard-disk image."
}
$diskCandidates = @($diskNames | ForEach-Object {
    $candidatePath = [System.IO.Path]::GetFullPath((Join-Path $resolvedProfile $_))
    if (-not $candidatePath.StartsWith(
        $resolvedProfile + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "The configured disk path leaves the VM profile: $_"
    }
    Get-Item -LiteralPath $candidatePath -ErrorAction Stop
})

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
# Name the backup after the profile it actually came from. This was hardcoded
# to Win86SE, so every profile's backup claimed to be that one - the contents
# were right and only the label lied, which is the worse failure: it is not
# visible until someone restores the wrong disk over a working guest.
$destination = Join-Path $BackupRoot ("{0}-pre-velocity9x-{1}" -f
    $profileItem.Name, $stamp)
if (Test-Path -LiteralPath $destination) {
    throw "Backup destination already exists: $destination"
}

New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null
New-Item -ItemType Directory -Path $destination | Out-Null
Copy-Item -LiteralPath $configPath -Destination $destination
foreach ($sourceDisk in $diskCandidates) {
    Copy-Item -LiteralPath $sourceDisk.FullName -Destination $destination
}
$nvrSource = Join-Path $resolvedProfile "nvr"
if (Test-Path -LiteralPath $nvrSource) {
    Copy-Item -LiteralPath $nvrSource -Destination $destination -Recurse
}

$manifestPath = Join-Path $destination "VELOCITY9X-BACKUP-SHA256.TXT"
$hashLines = Get-ChildItem -LiteralPath $destination -Recurse -File |
    Where-Object { $_.FullName -ne $manifestPath } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($destination.Length).TrimStart('\')
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
        "$hash  $relative"
    }
Set-Content -LiteralPath $manifestPath -Encoding Ascii -Value $hashLines

foreach ($sourceDisk in $diskCandidates) {
    $copiedDisk = Join-Path $destination $sourceDisk.Name
    if (-not (Test-Path -LiteralPath $copiedDisk) -or
        (Get-Item -LiteralPath $copiedDisk).Length -ne $sourceDisk.Length) {
        throw "Cold backup verification failed for $($sourceDisk.Name)."
    }
}

Write-Output "Cold VM profile backup completed: $destination"
Write-Output "Hash manifest: $manifestPath"
