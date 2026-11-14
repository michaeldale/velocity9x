[CmdletBinding()]
param([string]$BuildId)

# V9XHEAP.EXE: where DirectDraw places surfaces when the driver publishes a
# texture-only second heap. Phase 0 of
# docs\plans\intel-gen3-system-memory-textures.md.

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot 'build\ddraw-heap-probe'
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'ddraw-heap-local'
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$source = Join-Path $repoRoot 'tools\diag\ddraw_heap_probe_win32.c'
$exe = Join-Path $outputDir 'V9XHEAP.EXE'
$obj = Join-Path $outputDir 'v9xheap.obj'
$map = Join-Path $outputDir 'v9xheap.map'
$lnk = Join-Path $outputDir 'v9xheap.lnk'
$build = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Executable $exe -Object $obj -MapFile $map `
    -LinkFile $lnk -LibraryNames @('kernel32.lib', 'user32.lib') `
    -CompileArguments @('-bt=nt', '-zq', '-wx', '-zl', '-s',
                        "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$obj", $source) `
    -LinkOptions @("option start='_V9xHeapProbeEntry@0'", 'option stack=65536')
foreach ($name in @('LoadLibraryA', 'GetProcAddress', 'GetDesktopWindow')) {
    if ($build.DumpText -notmatch "(?m)\s$([regex]::Escape($name))\s*$") {
        throw "V9XHEAP.EXE is missing import $name."
    }
}
Write-Output "Built DirectDraw heap probe: $exe"
