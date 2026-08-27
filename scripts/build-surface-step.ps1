[CmdletBinding()]
param([string]$BuildId)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\surface-step"
. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "surface-step-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "BuildId contains unsupported characters."
}

$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { "C:\WATCOM" }
$compiler = Join-Path $watcomRoot "binnt64\wcc386.exe"
$linker = Join-Path $watcomRoot "binnt64\wlink.exe"
$dumper = Join-Path $watcomRoot "binnt64\wdump.exe"
$libraries = @("kernel32.lib", "user32.lib", "gdi32.lib") | ForEach-Object {
    Join-Path $watcomRoot "lib386\nt\$_"
}
foreach ($input in @($compiler, $linker, $dumper) + $libraries) {
    if (-not (Test-Path -LiteralPath $input)) { throw "Missing build input $input" }
}
$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$source = Join-Path $repoRoot "tools\diag\surface_step_win32.c"
$object = Join-Path $outputDir "surface_step_win32.obj"
$executable = Join-Path $outputDir "v9xsurf.exe"
$linkFile = Join-Path $outputDir "v9xsurf.lnk"
& $compiler "-bt=nt" "-zq" "-wx" "-zl" "-s" `
    "-i=$(Join-Path $repoRoot 'include')" `
    "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) { throw "Surface-step compilation failed." }
$linkLines = @(
    "format windows nt", "runtime windows=4.0", "option quiet",
    "option nodefaultlibs", "option start='_V9xSurfaceStepEntry@0'",
    "option stack=65536", "name '$executable'", "file '$object'"
)
$linkLines += $libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Value $linkLines -Encoding Ascii
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) { throw "Surface-step link failed." }

$bytes = [IO.File]::ReadAllBytes($executable)
$text = [Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @($BuildId, "Velocity9x surface step", "PASS-completed")) {
    if (-not $text.Contains($marker)) { throw "Surface-step output lacks $marker" }
}
$dump = (@(& $dumper -e $executable 2>&1)) -join "`n"
foreach ($api in @("FillRect", "BitBlt", "StretchBlt", "SetPixel", "GetPixel", "GdiFlush",
                    "WritePrivateProfileStringA", "ExitProcess")) {
    if ($dump -notmatch "(?m)\s$api\s*$") { throw "Surface-step lacks $api" }
}
Write-Output "Built staged surface probe: $executable"
