# Build V9XIOTR.EXE, the readout for the -IoTrace mini-VDD's V86 port log.
# Runtime-free Win32, same shape as the other diagnostics.
[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\io-trace"

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "io-trace-local"
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
$compiler = Join-Path $watcomRoot "binnt64\wcc386.exe"
$linker = Join-Path $watcomRoot "binnt64\wlink.exe"
$libraries = @(
    (Join-Path $watcomRoot "lib386\nt\kernel32.lib")
)
foreach ($input in @($compiler, $linker) + $libraries) {
    if (-not (Test-Path -LiteralPath $input)) {
        throw "Required input is missing: $input"
    }
}
$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$source = Join-Path $repoRoot "tools\diag\io_trace_win32.c"
$object = Join-Path $outputDir "io_trace_win32.obj"
$executable = Join-Path $outputDir "v9xiotr.exe"
$mapFile = Join-Path $outputDir "v9xiotr.map"
$linkFile = Join-Path $outputDir "v9xiotr.lnk"
$includeDir = Join-Path $repoRoot "include"

& $compiler "-bt=nt" "-zq" "-wx" "-zl" "-s" `
    "-i=$includeDir" "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to compile the I/O trace readout."
}
$linkLines = @(
    "format windows nt",
    "runtime windows=4.0",
    "option quiet",
    "option nodefaultlibs",
    "option start='_V9xIoTraceEntry@0'",
    "option stack=65536",
    "option map='$mapFile'",
    "name '$executable'",
    "file '$object'"
)
$linkLines += $libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link the I/O trace readout."
}
Write-Output "Built I/O trace readout: $executable"
