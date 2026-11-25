[CmdletBinding()]
param([string]$BuildId)
# V9XCURSQ.EXE: what GDI's cursor leaves on a full-screen DirectDraw game
# that never hides it (tools\diag\cursq_probe_win32.c; issue
# 2026-11-13-evergreen-cursor-square-over-fullscreen-game.md).
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'cursq-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
$outputDir = Join-Path $repoRoot 'build\cursq-probe'
$source = Join-Path $repoRoot 'tools\diag\cursq_probe_win32.c'
$object = Join-Path $outputDir 'cursq_probe_win32.obj'
$executable = Join-Path $outputDir 'V9XCURSQ.EXE'
$result = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Object $object -Executable $executable `
    -MapFile (Join-Path $outputDir 'V9XCURSQ.map') `
    -LinkFile (Join-Path $outputDir 'V9XCURSQ.lnk') `
    -LibraryNames @('kernel32.lib', 'user32.lib', 'gdi32.lib') `
    -CompileArguments @('-bt=nt', '-zq', '-wx', '-we', '-zl', '-s',
        "-i=$(Join-Path $repoRoot 'include')", "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$object", $source) `
    -LinkOptions @("option start='_V9xCursorSquareProbeEntry@0'", 'option stack=65536') `
    -ToolDescription 'cursor square probe' `
    -VersionDescription 'Velocity9x cursor over a full-screen game probe' -BuildId $BuildId
$imports = @([regex]::Matches($result.DumpText, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL', 'GDI32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
Write-Output "Built cursor square probe: $executable"
