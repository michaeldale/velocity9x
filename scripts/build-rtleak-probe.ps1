[CmdletBinding()]
param([string]$BuildId)
# V9XRTLK.EXE: whether an offscreen render target's video memory comes back
# when it is released (docs\issues\2026-11-15-placed-render-targets-never-freed.md).
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'rtleak-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
$outputDir = Join-Path $repoRoot 'build\rtleak-probe'
$source = Join-Path $repoRoot 'tools\diag\rtleak_probe_win32.c'
$object = Join-Path $outputDir 'rtleak_probe_win32.obj'
$executable = Join-Path $outputDir 'V9XRTLK.EXE'
$result = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Object $object -Executable $executable `
    -MapFile (Join-Path $outputDir 'V9XRTLK.map') `
    -LinkFile (Join-Path $outputDir 'V9XRTLK.lnk') `
    -LibraryNames @('kernel32.lib', 'user32.lib') `
    -CompileArguments @('-bt=nt', '-zq', '-wx', '-we', '-zl', '-s',
        "-i=$(Join-Path $repoRoot 'include')", "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$object", $source) `
    -LinkOptions @("option start='_V9xRenderTargetLeakEntry@0'", 'option stack=65536') `
    -ToolDescription 'render target leak probe' `
    -VersionDescription 'Velocity9x render target leak probe' -BuildId $BuildId
$imports = @([regex]::Matches($result.DumpText, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
Write-Output "Built render target leak probe: $executable"
