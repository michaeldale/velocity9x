[CmdletBinding()]
param([string]$BuildId)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'texshape-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
$outputDir = Join-Path $repoRoot 'build\texshape-probe'
$source = Join-Path $repoRoot 'tools\diag\texshape_probe_win32.c'
$object = Join-Path $outputDir 'texshape_probe_win32.obj'
$executable = Join-Path $outputDir 'V9XTSHP.EXE'
$result = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Object $object -Executable $executable `
    -MapFile (Join-Path $outputDir 'V9XTSHP.map') `
    -LinkFile (Join-Path $outputDir 'V9XTSHP.lnk') `
    -LibraryNames @('kernel32.lib', 'user32.lib', 'gdi32.lib') `
    -CompileArguments @('-bt=nt', '-zq', '-wx', '-we', '-zl', '-s',
        "-i=$(Join-Path $repoRoot 'include')", "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$object", $source) `
    -LinkOptions @("option start='_V9xTexShapeProbeEntry@0'", 'option stack=65536') `
    -ToolDescription 'texture shape probe' `
    -VersionDescription 'Velocity9x non-square texture probe' -BuildId $BuildId
$imports = @([regex]::Matches($result.DumpText, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL', 'GDI32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
Write-Output "Built texture shape probe: $executable"
