[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$OutputDirectory
)

# Builds V9XR3DP.EXE, the render interface probe
# (tools\diag\r3d_probe_win32.c). Same code generation as the HAL, with
# the executable target; imports KERNEL32 and USER32 only.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'r3dp-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build\r3d-probe' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { 'C:\WATCOM' }
$compiler = Join-Path $watcomRoot 'binnt64\wcc386.exe'
$linker = Join-Path $watcomRoot 'binnt64\wlink.exe'
$dumper = Join-Path $watcomRoot 'binnt64\wdump.exe'
$env:WATCOM = $watcomRoot
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$source = Join-Path $repoRoot 'tools\diag\r3d_probe_win32.c'
$object = Join-Path $output 'r3d_probe_win32.obj'
& $compiler '-bt=nt' '-zq' '-wx' '-we' '-zl' '-s' `
    "-i=$(Join-Path $repoRoot 'include')" `
    "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
$exe = Join-Path $output 'V9XR3DP.EXE'
$linkFile = Join-Path $output 'r3d-probe.lnk'
$lines = @('format windows nt', 'runtime windows=4.0', 'option quiet',
    'option nodefaultlibs', "option start='_V9xRenderInterfaceProbeEntry@0'", 'option stack=65536',
    "name '$exe'", "file '$object'",
    "library '$(Join-Path $watcomRoot 'lib386\nt\kernel32.lib')'",
    "library '$(Join-Path $watcomRoot 'lib386\nt\user32.lib')'")
Set-Content -LiteralPath $linkFile -Value $lines -Encoding Ascii
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) { throw 'render interface probe link failed.' }
$dump = (@(& $dumper -e $exe 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'render interface probe import audit failed.' }
$imports = @([regex]::Matches($dump, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
Write-Output "Built render interface probe: $exe"
