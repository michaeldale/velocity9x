[CmdletBinding()]
param([string]$BuildId)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'watch-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
$outputDir = Join-Path $repoRoot 'build\watch-probe'
$source = Join-Path $repoRoot 'tools\diag\watch_probe_win32.c'
$object = Join-Path $outputDir 'watch_probe_win32.obj'
$executable = Join-Path $outputDir 'V9XWATP.EXE'
$result = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Object $object -Executable $executable `
    -MapFile (Join-Path $outputDir 'V9XWATP.map') `
    -LinkFile (Join-Path $outputDir 'V9XWATP.lnk') `
    -LibraryNames @('kernel32.lib', 'user32.lib') `
    -CompileArguments @('-bt=nt', '-zq', '-wx', '-we', '-zl', '-s',
        "-i=$(Join-Path $repoRoot 'include')", "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$object", $source) `
    -LinkOptions @("option start='_V9xWatchProbeEntry@0'", 'option stack=65536') `
    -ToolDescription 'process watch probe' `
    -VersionDescription 'Velocity9x process watch probe' -BuildId $BuildId
$imports = @([regex]::Matches($result.DumpText, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
Write-Output "Built process watch probe: $executable"
