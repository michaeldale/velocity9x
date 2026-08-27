[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = "C:\98DDK"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\matrox-inventory"
. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "mga2-inventory-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "BuildId contains unsupported characters."
}

$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { "C:\WATCOM" }
$compiler = Join-Path $watcomRoot "binnt64\wcc386.exe"
$linker = Join-Path $watcomRoot "binnt64\wlink.exe"
$dumper = Join-Path $watcomRoot "binnt64\wdump.exe"
$libraries = @(
    (Join-Path $watcomRoot "lib386\nt\kernel32.lib"),
    (Join-Path $watcomRoot "lib386\nt\user32.lib"),
    (Join-Path $watcomRoot "lib386\nt\gdi32.lib"),
    (Join-Path $watcomRoot "lib386\nt\advapi32.lib"),
    (Join-Path $watcomRoot "lib386\nt\ddk\cfgmgr32.lib")
)
$required = @($compiler, $linker, $dumper,
              (Join-Path $DdkRoot "inc\win98\cfgmgr32.h")) + $libraries
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count -ne 0) { throw "Missing Matrox inventory inputs: $($missing -join ', ')" }

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt');$(Join-Path $DdkRoot 'inc\win98')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$source = Join-Path $repoRoot "tools\diag\matrox_inventory_win32.c"
$object = Join-Path $outputDir "matrox_inventory_win32.obj"
$executable = Join-Path $outputDir "v9xmga.exe"
$mapFile = Join-Path $outputDir "v9xmga.map"
$linkFile = Join-Path $outputDir "v9xmga.lnk"

& $compiler "-bt=nt" "-zq" "-wx" "-zl" "-s" `
    "-i=$(Join-Path $repoRoot 'include')" `
    "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) { throw "Open Watcom failed to compile the Matrox inventory probe." }

$linkLines = @(
    "format windows nt", "runtime windows=4.0", "option quiet",
    "option nodefaultlibs", "option start='_V9xMatroxInventoryEntry@0'",
    "option stack=65536", "option map='$mapFile'", "name '$executable'",
    "file '$object'"
) + ($libraries | ForEach-Object { "library '$_'" })
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) { throw "Open Watcom failed to link the Matrox inventory probe." }

$dumpText = (@(& $dumper -e $executable 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) { throw "Could not inspect the Matrox inventory probe." }
foreach ($api in @("CM_Locate_DevNodeA", "CM_Get_First_Log_Conf",
                    "CM_Get_Next_Res_Des", "CM_Get_Res_Des_Data",
                    "CreateFileA", "WriteFile")) {
    if ($dumpText -notmatch "(?m)\s$([regex]::Escape($api))\s*$") {
        throw "Matrox inventory probe is missing import $api."
    }
}
if ($dumpText -match "GetCommandLineW|GetModuleFileNameW|__CHK") {
    throw "Matrox inventory probe contains an incompatible runtime import."
}
Write-Output "Built read-only Matrox inventory probe: $executable"
