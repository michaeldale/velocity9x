[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$OutputDirectory
)

# Builds GLIDE2X.DLL, Glide 2.x over the render interface (src\glide,
# docs\plans\glide-2x-wrapper.md). Its exports, their indexes and the stubs
# for the ones not yet written are generated from
# src\glide\glide_entrypoints.psd1 by scripts\lib\glide-exports.ps1. Same
# code generation as the OpenGL ICD; a per-process DLL, not shared. Imports
# KERNEL32 and USER32 only.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\glide-exports.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'glide-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build\glide' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { 'C:\WATCOM' }
$compiler = Join-Path $watcomRoot 'binnt64\wcc386.exe'
$linker = Join-Path $watcomRoot 'binnt64\wlink.exe'
$dumper = Join-Path $watcomRoot 'binnt64\wdump.exe'
$env:WATCOM = $watcomRoot
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $output | Out-Null

$null = Write-V9xGlideExportHeader -RepoRoot $repoRoot -OutputDir $output
$entries = Get-V9xGlideEntries -RepoRoot $repoRoot

# glide_dll.c is the platform file: the exports, the log and the DLL entry.
$objects = @()
foreach ($name in @('glide_dll')) {
    $source = Join-Path $repoRoot "src\glide\$name.c"
    $object = Join-Path $output "$name.obj"
    & $compiler '-bt=nt' '-bd' '-zq' '-wx' '-we' '-zl' '-s' '-ox' `
        "-i=$(Join-Path $repoRoot 'include')" "-i=$output" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
    $objects += $object
}

$dll = Join-Path $output 'glide2x.dll'
$mapFile = Join-Path $output 'glide2x.map'
$linkFile = Join-Path $output 'glide2x.lnk'
# A game imports the decorated names, _grDrawTriangle@12 and so on, exactly
# as retail GLIDE2X.DLL exports them; the export name is the symbol.
$symbols = @($entries | ForEach-Object { Get-V9xGlideExportSymbol -Entry $_ })
$lines = @('format windows nt dll', 'runtime windows=4.0', 'option quiet',
    'option nodefaultlibs', "option start='_V9xGlideEntry@12'",
    "alias '__DLLstart_'='_V9xGlideEntry@12'",
    "option map='$mapFile'", "option modname='GLIDE2X'", "name '$dll'")
$lines += $objects | ForEach-Object { "file '$_'" }
$lines += $symbols | ForEach-Object { "export '$_'" }
$lines += @(
    "library '$(Join-Path $watcomRoot 'lib386\nt\kernel32.lib')'",
    "library '$(Join-Path $watcomRoot 'lib386\nt\user32.lib')'")
Set-Content -LiteralPath $linkFile -Value $lines -Encoding Ascii
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) { throw 'Glide DLL link failed.' }
Add-V9xVersionResource -RepoRoot $repoRoot -WatcomRoot $watcomRoot -Image $dll `
    -BuildId $BuildId -FileDescription 'Velocity9x Glide 2.x over the render interface' `
    -Kind dll

$dump = (@(& $dumper -e $dll 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Glide DLL import audit failed.' }
$imports = @([regex]::Matches($dump, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
$exported = @([regex]::Matches($dump, '_(gr|gu|Convert)\w*@\d+') | ForEach-Object Value |
    Sort-Object -Unique)
$missing = @($symbols | Where-Object { $_ -notin $exported })
if ($missing.Count) { throw "The Glide DLL does not export: $($missing -join ', ')" }
$extra = @($exported | Where-Object { $_ -notin $symbols })
if ($extra.Count) { throw "The Glide DLL exports names outside the manifest: $($extra -join ', ')" }
$imageText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($dll))
if (-not $imageText.Contains($BuildId)) { throw 'The Glide DLL is missing the build identifier.' }
Write-Output "Built Glide DLL: $dll"
Write-Output "Verified imports: $($imports -join ', '); exports: $($symbols.Count)"
