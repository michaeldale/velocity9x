[CmdletBinding()]
param([string]$BuildId)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'prof-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
$outputDir = Join-Path $repoRoot 'build\profile-probe'
$source = Join-Path $repoRoot 'tools\diag\profile_win32.c'
$object = Join-Path $outputDir 'profile_win32.obj'
$executable = Join-Path $outputDir 'V9XPROF.EXE'
$result = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Object $object -Executable $executable `
    -MapFile (Join-Path $outputDir 'V9XPROF.map') `
    -LinkFile (Join-Path $outputDir 'V9XPROF.lnk') `
    -LibraryNames @('kernel32.lib', 'user32.lib') `
    -CompileArguments @('-bt=nt', '-zq', '-wx', '-we', '-zl', '-s',
        "-i=$(Join-Path $repoRoot 'include')", "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$object", $source) `
    -LinkOptions @("option start='_V9xProfileEntry@0'", 'option stack=65536') `
    -ToolDescription 'sampling profiler' `
    -VersionDescription 'Velocity9x sampling profiler' -BuildId $BuildId
$imports = @([regex]::Matches($result.DumpText, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
Write-Output "Built sampling profiler: $executable"
