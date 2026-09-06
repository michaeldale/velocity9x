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
