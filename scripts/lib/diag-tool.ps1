# Shared Open Watcom plumbing for the small, single-source diagnostic tools.
#
# This resolves and validates the toolchain, creates the output directory,
# compiles, links, and obtains the import listing.  The per-tool audit is
# deliberately left with each caller: the required imports and marker strings
# are part of that tool's contract, not generic build policy.
function Get-V9xDiagToolchain {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("Win32", "Dos")][string]$Target,
        [string[]]$LibraryNames = @(),
        [switch]$AllowBinntFallback
    )

    $watcomRoot = $env:WATCOM
    if (-not $watcomRoot -and (Test-Path -LiteralPath "C:\WATCOM")) {
        $watcomRoot = "C:\WATCOM"
    }
    if (-not $watcomRoot) {
        throw "Open Watcom was not found. Set WATCOM or install it at C:\WATCOM."
    }

    $toolDirectories = @((Join-Path $watcomRoot "binnt64"))
    if ($AllowBinntFallback -or $Target -eq "Dos") {
        $toolDirectories += Join-Path $watcomRoot "binnt"
    }
    $compilerName = if ($Target -eq "Win32") { "wcc386.exe" } else { "wcl.exe" }
    $toolDirectory = $toolDirectories | Where-Object {
        Test-Path -LiteralPath (Join-Path $_ $compilerName)
    } | Select-Object -First 1
    if (-not $toolDirectory) {
        throw "Open Watcom $compilerName was not found under $watcomRoot."
    }

    $toolchain = [ordered]@{
        WatcomRoot = $watcomRoot
        Compiler = Join-Path $toolDirectory $compilerName
        Libraries = @($LibraryNames | ForEach-Object {
            Join-Path $watcomRoot (Join-Path "lib386\nt" $_)
        })
    }
    if ($Target -eq "Win32") {
        $toolchain.Linker = Join-Path $toolDirectory "wlink.exe"
        $toolchain.Dumper = Join-Path $toolDirectory "wdump.exe"
    }
    $required = @($toolchain.Compiler) + @($toolchain.Libraries)
    if ($Target -eq "Win32") {
        $required += @($toolchain.Linker, $toolchain.Dumper)
    }
    $missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_) })
    if ($missing.Count -ne 0) {
        throw "Required Open Watcom inputs are missing: $($missing -join ', ')"
    }
    [pscustomobject]$toolchain
}

function Invoke-V9xDiagToolBuild {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("Win32", "Dos")][string]$Target,
        [Parameter(Mandatory = $true)][string]$OutputDir,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$CompileArguments,
        [string[]]$LibraryNames = @(),
        [string]$Object,
        [string]$MapFile,
        [string]$LinkFile,
        [string[]]$LinkOptions = @(),
        [string]$ToolDescription = "diagnostic tool",
        [switch]$AllowBinntFallback
    )

    $toolchain = Get-V9xDiagToolchain -Target $Target -LibraryNames $LibraryNames `
        -AllowBinntFallback:$AllowBinntFallback
    $env:WATCOM = $toolchain.WatcomRoot
    $env:Path = "$(Join-Path $toolchain.WatcomRoot 'binnt64');$(Join-Path $toolchain.WatcomRoot 'binnt');$env:Path"
    if ($Target -eq "Win32") {
        $env:INCLUDE = "$(Join-Path $toolchain.WatcomRoot 'h');$(Join-Path $toolchain.WatcomRoot 'h\nt')"
    } else {
        $env:INCLUDE = Join-Path $toolchain.WatcomRoot "h"
    }
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

    if ($Target -eq "Dos") {
        Push-Location $OutputDir
    }
    try {
        & $toolchain.Compiler @CompileArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Open Watcom failed to compile the $ToolDescription."
        }
    } finally {
        if ($Target -eq "Dos") {
            Pop-Location
        }
    }

    if ($Target -eq "Dos") {
        return [pscustomobject]@{ Executable = $Executable; Toolchain = $toolchain; DumpText = $null }
    }

    $linkLines = @(
        "format windows nt",
        "runtime windows=4.0",
        "option quiet",
        "option nodefaultlibs"
    ) + $LinkOptions + @(
        "option map='$MapFile'",
        "name '$Executable'",
        "file '$Object'"
    ) + @($toolchain.Libraries | ForEach-Object { "library '$_'" })
    Set-Content -LiteralPath $LinkFile -Encoding Ascii -Value $linkLines
    & $toolchain.Linker "@$LinkFile"
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to link the $ToolDescription."
    }
    $dumpText = (@(& $toolchain.Dumper -e $Executable 2>&1)) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom could not inspect the $ToolDescription."
    }
    [pscustomobject]@{ Executable = $Executable; Toolchain = $toolchain; DumpText = $dumpText }
}
