[CmdletBinding()]
param([string]$BuildId)
# V9XFBCMP.EXE: compares the frame buffer (a DirectDraw Lock of the primary)
# with what GDI reads of the screen, which is the screen shadow on a chip
# that has one (tools\diag\fbcmp_probe_win32.c).
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'fbcmp-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
$outputDir = Join-Path $repoRoot 'build\fbcmp-probe'
$source = Join-Path $repoRoot 'tools\diag\fbcmp_probe_win32.c'
$object = Join-Path $outputDir 'fbcmp_probe_win32.obj'
$executable = Join-Path $outputDir 'V9XFBCMP.EXE'
$result = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Object $object -Executable $executable `
    -MapFile (Join-Path $outputDir 'V9XFBCMP.map') `
    -LinkFile (Join-Path $outputDir 'V9XFBCMP.lnk') `
    -LibraryNames @('kernel32.lib', 'user32.lib', 'gdi32.lib') `
    -CompileArguments @('-bt=nt', '-zq', '-wx', '-we', '-zl', '-s',
        "-i=$(Join-Path $repoRoot 'include')", "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$object", $source) `
    -LinkOptions @("option start='_V9xFbCompareProbeEntry@0'", 'option stack=65536') `
    -ToolDescription 'frame buffer comparison probe' `
    -VersionDescription 'Velocity9x frame buffer against GDI probe' -BuildId $BuildId
$imports = @([regex]::Matches($result.DumpText, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL', 'GDI32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
Write-Output "Built frame buffer comparison probe: $executable"
