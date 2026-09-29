# Shared helpers dot-sourced by the Velocity9x build scripts.

# Compiler arguments below use Windows PowerShell's native quoting rules.
# PowerShell 7.3+ otherwise escapes the embedded C-string quotes differently:
# Watcom treats -dV9X_BUILD_ID as a filename and MSVC receives stray escapes.
# Set this in the calling script's scope, leaving the interactive shell alone.
if (Test-Path Variable:PSNativeCommandArgumentPassing) {
    $PSNativeCommandArgumentPassing = 'Legacy'
}

function Get-V9xBuildId {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$Fallback
    )

    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) {
        return $Fallback
    }

    $revision = & $git.Source -C $RepoRoot rev-parse --short HEAD
    if ($LASTEXITCODE -ne 0 -or -not $revision) {
        return $Fallback
    }

    $pending = & $git.Source -C $RepoRoot status --porcelain
    if ($LASTEXITCODE -eq 0 -and $pending) {
        return "$revision-dirty"
    }
    return "$revision"
}

# The VERSIONINFO resource every shipped binary carries, so Explorer's
# Properties > Version tab, and anyone reading a bug report, can say which
# build is installed. The numbers come from build.h, like everything else;
# the build id rides in the FileVersion string. Returned as .rc text so a
# binary with its own resource script (the settings page) can append it.
#   Kind: 'dll', 'app', or 'display' (a Win16 display driver, VFT_DRV /
#   VFT2_DRV_DISPLAY). Win16 selects FILEOS VOS_DOS_WINDOWS16.
function Get-V9xVersionResourceText {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$BuildId,
        [Parameter(Mandatory = $true)][string]$FileDescription,
        [Parameter(Mandatory = $true)][string]$OriginalFilename,
        [Parameter(Mandatory = $true)][ValidateSet('dll', 'app', 'display')]
        [string]$Kind,
        [switch]$Win16
    )

    $headerPath = Join-Path $RepoRoot "include\velocity9x\build.h"
    $header = Get-Content -LiteralPath $headerPath -Raw
    $numbers = foreach ($part in 'MAJOR', 'MINOR', 'PATCH') {
        $match = [regex]::Match(
            $header, "(?m)^#define\s+V9X_VERSION_$part\s+(\d+)u\s*$")
        if (-not $match.Success) {
            throw "Could not read V9X_VERSION_$part from $headerPath."
        }
        [int]$match.Groups[1].Value
    }
    $version = Get-V9xProductVersion -RepoRoot $RepoRoot
    foreach ($text in @($BuildId, $FileDescription, $OriginalFilename)) {
        if ($text -match '["\\]') {
            throw "Version resource text may not contain quotes or backslashes: $text"
        }
    }
    # winver.h values, spelled out so no SDK header is needed.
    $fileType = switch ($Kind) { 'app' { 1 } 'dll' { 2 } 'display' { 3 } }
    $fileSubtype = if ($Kind -eq 'display') { 4 } else { 0 }
    $fileOs = if ($Win16) { '0x00010001L' } else { '0x00000004L' }
    $binary = '{0},{1},{2},0' -f $numbers[0], $numbers[1], $numbers[2]

    return @(
        "1 VERSIONINFO",
        "FILEVERSION $binary",
        "PRODUCTVERSION $binary",
        "FILEFLAGSMASK 0x3FL",
        "FILEFLAGS 0x0L",
        "FILEOS $fileOs",
        "FILETYPE $fileType",
        "FILESUBTYPE $fileSubtype",
        "BEGIN",
        "    BLOCK `"StringFileInfo`"",
        "    BEGIN",
        "        BLOCK `"040904E4`"",
        "        BEGIN",
        "            VALUE `"FileDescription`", `"$FileDescription\0`"",
        "            VALUE `"FileVersion`", `"$version ($BuildId)\0`"",
        "            VALUE `"InternalName`", `"$([IO.Path]::GetFileNameWithoutExtension($OriginalFilename))\0`"",
        "            VALUE `"OriginalFilename`", `"$OriginalFilename\0`"",
        "            VALUE `"ProductName`", `"Velocity9x\0`"",
        "            VALUE `"ProductVersion`", `"$version\0`"",
        "        END",
        "    END",
        "    BLOCK `"VarFileInfo`"",
        "    BEGIN",
        "        VALUE `"Translation`", 0x409, 1252",
        "    END",
        "END",
        ""
    ) -join "`r`n"
}

# Embeds the version resource into a linked image with Open Watcom's wrc and
# checks that RT_VERSION (16) is there afterwards. wrc replaces an image's
# resources wholesale, so a binary that has other resources must append the
# text above to its own script instead of calling this.
function Add-V9xVersionResource {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$WatcomRoot,
        [Parameter(Mandatory = $true)][string]$Image,
        [Parameter(Mandatory = $true)][string]$BuildId,
        [Parameter(Mandatory = $true)][string]$FileDescription,
        [Parameter(Mandatory = $true)][ValidateSet('dll', 'app', 'display')]
        [string]$Kind,
        [switch]$Win16
    )

    $resourceCompiler = Join-Path $WatcomRoot "binnt64\wrc.exe"
    $resourceFile = [IO.Path]::ChangeExtension($Image, ".ver.rc")
    $text = Get-V9xVersionResourceText -RepoRoot $RepoRoot -BuildId $BuildId `
        -FileDescription $FileDescription `
        -OriginalFilename ([IO.Path]::GetFileName($Image).ToUpperInvariant()) `
        -Kind $Kind -Win16:$Win16
    Set-Content -LiteralPath $resourceFile -Encoding Ascii -Value $text
    $target = if ($Win16) { "-bt=windows" } else { "-bt=nt" }
    & $resourceCompiler "-q" $target $resourceFile $Image
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to embed the version resource in $Image."
    }
    Assert-V9xVersionResource -WatcomRoot $WatcomRoot -Image $Image
}

function Assert-V9xVersionResource {
    param(
        [Parameter(Mandatory = $true)][string]$WatcomRoot,
        [Parameter(Mandatory = $true)][string]$Image
    )

    $dumper = Join-Path $WatcomRoot "binnt64\wdump.exe"
    $dump = (@(& $dumper -r $Image 2>&1)) -join "`n"
    # wdump lists a PE resource type as 8 hex digits at the start of a line
    # and an NE one as "Type number: 16".
    if ($LASTEXITCODE -ne 0 -or
        ($dump -notmatch '(?m)^00000010\s' -and
         $dump -notmatch '(?m)^\s*Type number: 16\s*$')) {
        throw "$Image carries no version resource after wrc."
    }
}

function Get-V9xProductVersion {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )

    $headerPath = Join-Path $RepoRoot "include\velocity9x\build.h"
    $header = Get-Content -LiteralPath $headerPath -Raw
    $match = [regex]::Match(
        $header, '(?m)^#define\s+V9X_VERSION_STRING\s+"([^"]+)"\s*$')
    if (-not $match.Success) {
        throw "Could not read V9X_VERSION_STRING from $headerPath."
    }
    return $match.Groups[1].Value
}
