[CmdletBinding()]
param(
    [string]$BuildId
)

# V9XMSDOS.EXE: set C:\MSDOS.SYS [Options] keys from Windows, for test
# machines whose boot menu cannot be seen (tools\diag\msdos_options_win32.c).

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\msdos-options"

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "msdos-options-local"
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
    (Join-Path $watcomRoot "lib386\nt\kernel32.lib")
)
$missingInputs = @(@($compiler, $linker, $dumper) + $libraries |
    Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missingInputs.Count -ne 0) {
    throw "Required msdos-options inputs are missing: $($missingInputs -join ', ')"
}

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$sources = @(
    (Join-Path $repoRoot "tools\diag\msdos_options_win32.c")
)
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $outputDir ([IO.Path]::GetFileNameWithoutExtension($source) + ".obj")
    & $compiler "-bt=nt" "-zq" "-wx" "-we" "-zl" "-s" `
        "-i=$(Join-Path $repoRoot 'include')" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to compile $source."
    }
    $objects += $object
}

$executable = Join-Path $outputDir "v9xmsdos.exe"
$mapFile = Join-Path $outputDir "v9xmsdos.map"
$linkFile = Join-Path $outputDir "v9xmsdos.lnk"
$linkLines = @(
    "format windows nt",
    "runtime windows=4.0",
    "option quiet",
    "option nodefaultlibs",
    "option start='_V9xMsdosEntry@0'",
    "option stack=65536",
    "option map='$mapFile'",
    "name '$executable'"
)
$linkLines += $objects | ForEach-Object { "file '$_'" }
$linkLines += $libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link the msdos options helper."
}
Add-V9xVersionResource -RepoRoot $repoRoot -WatcomRoot $watcomRoot -Image $executable `
    -BuildId $BuildId -FileDescription "Velocity9x MSDOS.SYS options helper" -Kind app

$dumpText = (@(& $dumper -e $executable 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom could not inspect the msdos options helper."
}
$dllNames = [regex]::Matches($dumpText, "DLL name = <([^>]+)>") |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpectedDlls = @($dllNames | Where-Object { $_ -notin @("KERNEL32.DLL") })
if ($unexpectedDlls.Count -ne 0 -or
    $dumpText -match "GetCommandLineW|GetModuleFileNameW|__CHK") {
    throw "The msdos options helper contains an incompatible runtime import."
}

Write-Output "Built Velocity9x MSDOS.SYS options helper: $executable"
Write-Output "Verified runtime-free imports: $($dllNames -join ', ')"
