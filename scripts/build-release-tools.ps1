# Build the host-side release tools into build\release-tools:
#
#   v9xsign.exe   derive the release public key, sign SIGNED.TXT, verify it
#   v9xunzip.exe  extract a release zip with the updater's own zip reader
#
# Both link the same src\common modules V9XUPD.EXE does, so a file v9xsign
# accepts and an archive v9xunzip extracts are ones the updater will too.
# These run on the developer's PC, not on Windows 98.
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\release-tools"

$watcomRoot = $env:WATCOM
if (-not $watcomRoot -and (Test-Path -LiteralPath "C:\WATCOM")) {
    $watcomRoot = "C:\WATCOM"
}
if (-not $watcomRoot) {
    throw "Open Watcom was not found. Set WATCOM or install it at C:\WATCOM."
}
$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
$compiler = Join-Path $watcomRoot "binnt64\wcl386.exe"
if (-not (Test-Path -LiteralPath $compiler)) {
    $compiler = Join-Path $watcomRoot "binnt\wcl386.exe"
}
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$tools = @(
    @{ Name = "v9xsign.exe"; Sources = @(
        "tools\release\v9xsign.c", "src\common\ed25519.c",
        "src\common\sha512.c") },
    @{ Name = "v9xunzip.exe"; Sources = @(
        "tools\release\v9xunzip.c", "src\common\zipread.c",
        "src\common\inflate.c", "src\common\crc32.c") }
)

Push-Location $outputDir
try {
    foreach ($tool in $tools) {
        $arguments = @("-bt=nt", "-zq", "-wx", "-we",
            "-i=$(Join-Path $repoRoot 'include')",
            "-fe=$(Join-Path $outputDir $tool.Name)") +
            @($tool.Sources | ForEach-Object { Join-Path $repoRoot $_ })
        & $compiler @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Open Watcom failed to build $($tool.Name)."
        }
        Write-Output "Built $(Join-Path $outputDir $tool.Name)"
    }
} finally {
    Pop-Location
}
