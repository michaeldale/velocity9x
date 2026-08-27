[CmdletBinding()]
param(
    [string]$BuildId
)

# Builds the Trio64 calling-context probe (tools\diag\trio_ctx_probe.c) twice
# from the one source: V9XTC32.EXE (Win32, runtime-free) and V9XTC16.EXE
# (Win16). See the source header for what the experiment isolates.

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\trio-ctx"

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "trio-ctx-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "BuildId may contain only letters, digits, dot, underscore, plus, and hyphen."
}

$watcomRoot = $env:WATCOM
if (-not $watcomRoot -and (Test-Path -LiteralPath "C:\WATCOM")) {
    $watcomRoot = "C:\WATCOM"
}
if (-not $watcomRoot) {
    throw "Open Watcom was not found. Set WATCOM or install it at C:\WATCOM."
}

$compiler386 = Join-Path $watcomRoot "binnt64\wcc386.exe"
$linker = Join-Path $watcomRoot "binnt64\wlink.exe"
$compiler16 = Join-Path $watcomRoot "binnt\wcl.exe"
$dumper = Join-Path $watcomRoot "binnt64\wdump.exe"
$libraries = @(
    (Join-Path $watcomRoot "lib386\nt\kernel32.lib"),
    (Join-Path $watcomRoot "lib386\nt\user32.lib"),
    (Join-Path $watcomRoot "lib386\nt\gdi32.lib")
)
$missingInputs = @(@($compiler386, $linker, $compiler16, $dumper) + $libraries |
    Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missingInputs.Count -ne 0) {
    throw "Required trio-ctx inputs are missing: $($missingInputs -join ', ')"
}

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$source = Join-Path $repoRoot "tools\diag\trio_ctx_probe.c"

# --- Win32 arm: same runtime-free pattern as build-gdi-smoke.ps1. ---
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
$object = Join-Path $outputDir "trio_ctx_win32.obj"
$exe32 = Join-Path $outputDir "v9xtc32.exe"
$linkFile = Join-Path $outputDir "v9xtc32.lnk"
& $compiler386 "-bt=nt" "-zq" "-wx" "-zl" "-s" `
    "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to compile the Win32 trio-ctx probe."
}
$linkLines = @(
    "format windows nt",
    "runtime windows=4.0",
    "option quiet",
    "option nodefaultlibs",
    "option start='_V9xTrioCtxEntry@0'",
    "option stack=65536",
    "name '$exe32'",
    "file '$object'"
)
$linkLines += $libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link the Win32 trio-ctx probe."
}
$dumpText = (@(& $dumper -e $exe32 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom could not inspect the Win32 trio-ctx probe."
}
$dllNames = [regex]::Matches($dumpText, "DLL name = <([^>]+)>") |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpectedDlls = @($dllNames | Where-Object {
    $_ -notin @("KERNEL32.DLL", "USER32.DLL", "GDI32.DLL")
})
if ($unexpectedDlls.Count -ne 0 -or $dumpText -match "__CHK") {
    throw "The Win32 trio-ctx probe contains an incompatible runtime import."
}

# --- Win16 arm: same pattern as build-win16-loader-probe.ps1. ---
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\win')"
$exe16 = Join-Path $outputDir "v9xtc16.exe"
Push-Location $outputDir
try {
    & $compiler16 "-bt=windows" "-l=windows" "-zq" "-wx" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fe=$exe16" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to build the Win16 trio-ctx probe."
    }
}
finally {
    Pop-Location
}
$bytes = [System.IO.File]::ReadAllBytes($exe16)
$newHeaderOffset = if ($bytes.Length -ge 64) {
    [System.BitConverter]::ToInt32($bytes, 0x3c)
} else { -1 }
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a -or
    $newHeaderOffset -lt 0 -or $newHeaderOffset + 1 -ge $bytes.Length -or
    $bytes[$newHeaderOffset] -ne 0x4e -or
    $bytes[$newHeaderOffset + 1] -ne 0x45) {
    throw "The Win16 trio-ctx probe is not an MZ/NE image."
}

Write-Output "Built Trio64 context probes: $exe32, $exe16"
