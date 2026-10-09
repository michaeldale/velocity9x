# Build GLIDE3X.DLL, the Glide 3.x census (docs\plans\glide-3x-wrapper.md,
# Phase 0): every export of 3dfx's GLIDE3X.DLL, logging to
# C:\V9XDIAG\V9XGLD3.LOG and drawing nothing. Not packaged: it is copied
# into a game's own folder for a census run.
[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\glide3-exports.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'glide3-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build\glide3' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { 'C:\WATCOM' }
$compiler = Join-Path $watcomRoot 'binnt64\wcc386.exe'
$linker = Join-Path $watcomRoot 'binnt64\wlink.exe'
$dumper = Join-Path $watcomRoot 'binnt64\wdump.exe'
$env:WATCOM = $watcomRoot
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $output | Out-Null

$null = Write-V9xGlide3ExportHeader -RepoRoot $repoRoot -OutputDir $output
$entries = Get-V9xGlide3Entries -RepoRoot $repoRoot

$source = Join-Path $repoRoot 'src\glide3\glide3_census.c'
$object = Join-Path $output 'glide3_census.obj'
& $compiler '-bt=nt' '-bd' '-zq' '-wx' '-we' '-zl' '-s' '-ox' `
    "-i=$(Join-Path $repoRoot 'include')" "-i=$output" `
    "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }

$dll = Join-Path $output 'glide3x.dll'
$mapFile = Join-Path $output 'glide3x.map'
$linkFile = Join-Path $output 'glide3x.lnk'
$symbols = @($entries | ForEach-Object { Get-V9xGlide3ExportSymbol -Entry $_ })
$lines = @('format windows nt dll', 'runtime windows=4.0', 'option quiet',
    'option nodefaultlibs', "option start='_V9xGlide3Entry@12'",
    "alias '__DLLstart_'='_V9xGlide3Entry@12'",
    "option map='$mapFile'", "option modname='GLIDE3X'", "name '$dll'",
    "file '$object'")
$lines += $symbols | ForEach-Object { "export '$_'" }
$lines += @(
    "library '$(Join-Path $watcomRoot 'lib386\nt\kernel32.lib')'",
    "library '$(Join-Path $watcomRoot 'lib386\nt\user32.lib')'")
Set-Content -LiteralPath $linkFile -Value $lines -Encoding Ascii
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) { throw 'Glide 3 DLL link failed.' }
Add-V9xVersionResource -RepoRoot $repoRoot -WatcomRoot $watcomRoot -Image $dll `
    -BuildId $BuildId -FileDescription 'Velocity9x Glide 3.x census' -Kind dll

$dump = (@(& $dumper -e $dll 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Glide 3 DLL import audit failed.' }
$imports = @([regex]::Matches($dump, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
$exported = @([regex]::Matches($dump, '_(gr|gu|tx)\w*@\d+') | ForEach-Object Value |
    Sort-Object -Unique)
$missing = @($symbols | Where-Object { $_ -notin $exported })
if ($missing.Count) { throw "The Glide 3 DLL does not export: $($missing -join ', ')" }
$extra = @($exported | Where-Object { $_ -notin $symbols })
if ($extra.Count) { throw "The Glide 3 DLL exports names outside the manifest: $($extra -join ', ')" }
$imageText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($dll))
if (-not $imageText.Contains($BuildId)) { throw 'The Glide 3 DLL is missing the build identifier.' }
Write-Output "Built Glide 3 census DLL: $dll"
Write-Output "Verified imports: $($imports -join ', '); exports: $($symbols.Count)"
