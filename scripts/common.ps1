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
    $strings = [ordered]@{
        FileDescription  = $FileDescription
        FileVersion      = "$version ($BuildId)"
        InternalName     = [IO.Path]::GetFileNameWithoutExtension($OriginalFilename)
        OriginalFilename = $OriginalFilename
        ProductName      = 'Velocity9x'
        ProductVersion   = $version
    }

    # wrc's 16-bit VERSIONINFO declares every string value one byte longer
    # than it writes (with or without an explicit "\0"). Where that byte
    # crosses a 4-byte boundary a length overshoots by 4 and
    # GetFileVersionInfo rejects the whole resource: DxDiag showed
    # V9XDISP.DRV's version as "()" (2026-10-03). So a Win16 image gets the
    # block laid out here and handed to wrc as raw data of type 16.
    if ($Win16) {
        # The signature as text: PowerShell 5.1 reads 0xFEEF04BD as negative.
        $fixed = [uint32[]]@(
            [Convert]::ToUInt32('FEEF04BD', 16), 0x00010000,
            (($numbers[0] -shl 16) -bor $numbers[1]), ($numbers[2] -shl 16),
            (($numbers[0] -shl 16) -bor $numbers[1]), ($numbers[2] -shl 16),
            0x3F, 0, 0x00010001, $fileType, $fileSubtype, 0, 0)
        $bytes = New-V9xVersionBlock16 -Fixed $fixed -Strings $strings
        $words = for ($i = 0; $i -lt $bytes.Length; $i += 2) {
            # As int: PowerShell shifts a [byte] within a byte, to zero.
            '0x{0:X4}' -f ([int]$bytes[$i] -bor ([int]$bytes[$i + 1] -shl 8))
        }
        $lines = for ($i = 0; $i -lt $words.Count; $i += 8) {
            '    ' + (($words[$i..([Math]::Min($i + 7, $words.Count - 1))]) -join ', ')
        }
        return (@('1 16', 'BEGIN', ($lines -join ",`r`n"), 'END', '') -join "`r`n")
    }

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

# One node of a 16-bit VERSIONINFO tree: WORD wLength, WORD wValueLength,
# the key and its NUL, padding to a DWORD, the value, then each child on a
# DWORD boundary. wLength covers the node and its children, not the
# padding after the last of them.
function New-V9xVersionNode16 {
    param(
        [Parameter(Mandatory = $true)][string]$Key,
        [byte[]]$Value = @(),
        [object[]]$Children = @()
    )

    $node = New-Object System.Collections.Generic.List[byte]
    $node.AddRange([byte[]](0, 0, 0, 0))
    $node.AddRange([Text.Encoding]::ASCII.GetBytes($Key))
    $node.Add(0)
    while (($node.Count % 4) -ne 0) { $node.Add(0) }
    $node.AddRange($Value)
    foreach ($child in $Children) {
        while (($node.Count % 4) -ne 0) { $node.Add(0) }
        $node.AddRange([byte[]]$child)
    }
    $node[0] = [byte]([int]$node.Count -band 0xFF)
    $node[1] = [byte]([int]$node.Count -shr 8)
    $node[2] = [byte]([int]$Value.Length -band 0xFF)
    $node[3] = [byte]([int]$Value.Length -shr 8)
    return , $node.ToArray()
}

# The whole 16-bit VS_VERSION_INFO: the fixed info, the strings in code
# page 040904E4, and the translation they name. Padded to a whole number
# of words, which is how wrc takes raw data.
function New-V9xVersionBlock16 {
    param(
        [Parameter(Mandatory = $true)][uint32[]]$Fixed,
        [Parameter(Mandatory = $true)][System.Collections.Specialized.OrderedDictionary]$Strings
    )

    $fixedBytes = New-Object System.Collections.Generic.List[byte]
    foreach ($dword in $Fixed) { $fixedBytes.AddRange([BitConverter]::GetBytes([uint32]$dword)) }
    $entries = foreach ($name in $Strings.Keys) {
        $text = [Text.Encoding]::ASCII.GetBytes([string]$Strings[$name]) + [byte[]](0)
        , (New-V9xVersionNode16 -Key $name -Value $text)
    }
    $table = New-V9xVersionNode16 -Key '040904E4' -Children $entries
    $stringInfo = New-V9xVersionNode16 -Key 'StringFileInfo' -Children @(, $table)
    $translation = New-V9xVersionNode16 -Key 'Translation' -Value ([byte[]](0x09, 0x04, 0xE4, 0x04))
    $varInfo = New-V9xVersionNode16 -Key 'VarFileInfo' -Children @(, $translation)
    $root = New-V9xVersionNode16 -Key 'VS_VERSION_INFO' -Value $fixedBytes.ToArray() `
        -Children @($stringInfo, $varInfo)
    if (($root.Length % 2) -ne 0) { $root += [byte[]](0) }
    return , $root
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
    # Present is not readable: a resource whose lengths disagree is there
    # for wdump and empty for Windows (V9XDISP.DRV until 2026-10-03).
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Image)
    if ([string]::IsNullOrEmpty($info.FileVersion)) {
        throw "$Image carries a version resource Windows cannot read."
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
