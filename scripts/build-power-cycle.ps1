[CmdletBinding()]
param([string]$BuildId)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\power-cycle"

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "power-cycle-local"
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
$dumper = Join-Path $watcomRoot "binnt64\wdump.exe"
$libraries = @(
    (Join-Path $watcomRoot "lib386\nt\kernel32.lib"),
    (Join-Path $watcomRoot "lib386\nt\user32.lib")
)
$missingInputs = @(@($compiler, $linker, $dumper) + $libraries |
    Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missingInputs.Count -ne 0) {
    throw "Required power-cycle inputs are missing: $($missingInputs -join ', ')"
}

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$source = Join-Path $repoRoot "tools\diag\power_cycle_win32.c"
$object = Join-Path $outputDir "power_cycle_win32.obj"
$executable = Join-Path $outputDir "v9xpwr.exe"
$mapFile = Join-Path $outputDir "v9xpwr.map"
$linkFile = Join-Path $outputDir "v9xpwr.lnk"

& $compiler "-bt=nt" "-zq" "-wx" "-zl" "-s" `
    "-i=$(Join-Path $repoRoot 'include')" `
    "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to compile the monitor power-cycle test."
}

$linkLines = @(
    "format windows nt",
    "runtime windows=4.0",
    "option quiet",
    "option nodefaultlibs",
    "option start='_V9xPowerCycleEntry@0'",
    "option stack=32768",
    "option map='$mapFile'",
    "name '$executable'",
    "file '$object'"
)
$linkLines += $libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link the monitor power-cycle test."
}

$bytes = [System.IO.File]::ReadAllBytes($executable)
$newHeaderOffset = if ($bytes.Length -ge 64) {
    [System.BitConverter]::ToInt32($bytes, 0x3c)
} else { -1 }
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or
    $bytes[1] -ne 0x5a -or $newHeaderOffset -lt 0 -or
    $newHeaderOffset + 1 -ge $bytes.Length -or
    $bytes[$newHeaderOffset] -ne 0x50 -or
    $bytes[$newHeaderOffset + 1] -ne 0x45) {
    throw "The monitor power-cycle test is not an MZ/PE executable."
}
$imageText = [System.Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @($BuildId, "Velocity9xPower", "C:\V9XDIAG\V9XPWR.INI")) {
    if (-not $imageText.Contains($marker)) {
        throw "The monitor power-cycle test is missing marker $marker."
    }
}

$dumpText = (@(& $dumper -e $executable 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom could not inspect the monitor power-cycle test."
}
foreach ($requiredApi in @("SendMessageTimeoutA", "Sleep",
                            "WritePrivateProfileStringA", "ExitProcess")) {
    if ($dumpText -notmatch "(?m)\s$([regex]::Escape($requiredApi))\s*$") {
        throw "The monitor power-cycle test is missing import $requiredApi."
    }
}
if ($dumpText -match "GetCommandLineW|GetModuleFileNameW|__CHK") {
    throw "The monitor power-cycle test contains an incompatible runtime import."
}

Write-Output "Built Windows 98 monitor power-cycle test: $executable"
