[CmdletBinding()]
param([string]$BuildId)

# V9XBLTX.EXE: DirectDraw fills and copies between video-memory surfaces,
# each compared byte for byte, padding included, with a system-memory
# model. Phase 3 of docs\plans\ati-oland-gfx6.md; any family can run it.

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot 'build\blt-exact-probe'
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'blt-exact-local'
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$source = Join-Path $repoRoot 'tools\diag\blt_exact_win32.c'
$exe = Join-Path $outputDir 'V9XBLTX.EXE'
$obj = Join-Path $outputDir 'v9xbltx.obj'
$map = Join-Path $outputDir 'v9xbltx.map'
$lnk = Join-Path $outputDir 'v9xbltx.lnk'
$build = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Executable $exe -Object $obj -MapFile $map `
    -LinkFile $lnk -LibraryNames @('kernel32.lib', 'user32.lib') `
    -CompileArguments @('-bt=nt', '-zq', '-wx', '-zl', '-s',
                        "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$obj", $source) `
    -LinkOptions @("option start='_V9xBltExactEntry@0'", 'option stack=65536')
foreach ($name in @('LoadLibraryA', 'GetProcAddress', 'VirtualAlloc')) {
    if ($build.DumpText -notmatch "(?m)\s$([regex]::Escape($name))\s*$") {
        throw "V9XBLTX.EXE is missing import $name."
    }
}
Write-Output "Built byte-exact blit probe: $exe"
