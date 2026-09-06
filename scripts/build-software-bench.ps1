[CmdletBinding()]
param(
    [string]$BuildId,
    # A captured pre-change rasterizer permits same-boot A/B measurements.
    [string]$RasterSource,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'softbench-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
if (-not $RasterSource) { $RasterSource = Join-Path $repoRoot 'src\display32\d3d\d3d_raster.c' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build\software-bench' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { 'C:\WATCOM' }
$compiler = Join-Path $watcomRoot 'binnt64\wcc386.exe'
$linker = Join-Path $watcomRoot 'binnt64\wlink.exe'
$dumper = Join-Path $watcomRoot 'binnt64\wdump.exe'
$env:WATCOM = $watcomRoot
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$sources = @((Join-Path $repoRoot 'tools\diag\software_bench_win32.c'), [IO.Path]::GetFullPath($RasterSource))
$objects = @()
for ($index = 0; $index -lt $sources.Count; ++$index) {
    $object = Join-Path $output "source$index.obj"
    # Match HAL code generation, with the executable (not DLL) target.
    & $compiler '-bt=nt' '-zq' '-wx' '-we' '-zl' '-s' `
        "-i=$(Join-Path $repoRoot 'include')" "-i=$(Join-Path $repoRoot 'src\display32\d3d')" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $sources[$index]
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $($sources[$index])" }
    $objects += $object
}
$exe = Join-Path $output 'V9XSOFT.EXE'
$linkFile = Join-Path $output 'software-bench.lnk'
$lines = @('format windows nt', 'runtime windows=4.0', 'option quiet',
    'option nodefaultlibs', "option start='_V9xSoftwareBenchEntry@0'", 'option stack=65536',
    "name '$exe'") + @($objects | ForEach-Object { "file '$_'" }) + @(
    "library '$(Join-Path $watcomRoot 'lib386\nt\kernel32.lib')'",
    "library '$(Join-Path $watcomRoot 'lib386\nt\user32.lib')'")
Set-Content -LiteralPath $linkFile -Value $lines -Encoding Ascii
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) { throw 'Software benchmark link failed.' }
$dump = (@(& $dumper -e $exe 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Software benchmark import audit failed.' }
$imports = @([regex]::Matches($dump, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
Write-Output "Built software rasterizer benchmark: $exe"
