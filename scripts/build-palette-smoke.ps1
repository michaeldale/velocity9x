[CmdletBinding()]
param([string]$BuildId)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\palette-smoke"
. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "palette-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw "Invalid BuildId." }
$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { "C:\WATCOM" }
$compiler = Join-Path $watcomRoot "binnt64\wcc386.exe"
$linker = Join-Path $watcomRoot "binnt64\wlink.exe"
$dumper = Join-Path $watcomRoot "binnt64\wdump.exe"
$libraries = @("kernel32.lib", "user32.lib", "gdi32.lib") |
    ForEach-Object { Join-Path $watcomRoot "lib386\nt\$_" }
foreach ($path in @($compiler, $linker, $dumper) + $libraries) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing build input: $path" }
}
$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$source = Join-Path $repoRoot "tools\diag\palette_smoke_win32.c"
$object = Join-Path $outputDir "palette_smoke_win32.obj"
$executable = Join-Path $outputDir "v9xpal.exe"
$map = Join-Path $outputDir "v9xpal.map"
$link = Join-Path $outputDir "v9xpal.lnk"
& $compiler "-bt=nt" "-zq" "-wx" "-zl" "-s" `
    "-i=$(Join-Path $repoRoot 'include')" `
    "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) { throw "Palette test compilation failed." }
$lines = @("format windows nt", "runtime windows=4.0", "option quiet",
    "option nodefaultlibs", "option start='_V9xPaletteSmokeEntry@0'",
    "option stack=65536", "option map='$map'", "name '$executable'",
    "file '$object'") + ($libraries | ForEach-Object { "library '$_'" })
Set-Content -LiteralPath $link -Encoding Ascii -Value $lines
& $linker "@$link"
if ($LASTEXITCODE -ne 0) { throw "Palette test link failed." }
$dump = (@(& $dumper -e $executable 2>&1)) -join "`n"
foreach ($api in @("AnimatePalette", "GetPaletteEntries", "GetPixel",
                    "WritePrivateProfileStringA")) {
    if ($dump -notmatch "(?m)\s$([regex]::Escape($api))\s*$") {
        throw "Palette test is missing import $api."
    }
}
if ($dump -match "GetCommandLineW|__CHK") {
    throw "Palette test contains an incompatible runtime import."
}
Write-Output "Built Windows 98 palette animation test: $executable"
