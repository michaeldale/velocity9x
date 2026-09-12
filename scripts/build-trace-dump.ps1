[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\trace-dump"

. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "lib\diag-tool.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "trace-dump-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "BuildId may contain only letters, digits, dot, underscore, plus, and hyphen."
}

$source = Join-Path $repoRoot "tools\diag\d3d_trace_dump_win32.c"
$object = Join-Path $outputDir "d3d_trace_dump_win32.obj"
$executable = Join-Path $outputDir "v9xtrace.exe"

$result = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Object $object -Executable $executable `
    -MapFile (Join-Path $outputDir "v9xtrace.map") `
    -LinkFile (Join-Path $outputDir "v9xtrace.lnk") `
    -LibraryNames @("kernel32.lib", "user32.lib", "gdi32.lib") `
    -CompileArguments @("-bt=nt", "-zq", "-wx", "-zl", "-s",
        "-i=$(Join-Path $repoRoot 'include')",
        "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$object", $source) `
    -LinkOptions @("option start='_V9xTraceDumpEntry@0'", "option stack=65536") `
    -ToolDescription "trace dump tool"

# The per-tool contract: a Windows 98 guest tool may import only these three
# DLLs and none of the Watcom runtime entry points.
$dumpText = $result.DumpText
$dllNames = [regex]::Matches($dumpText, "DLL name = <([^>]+)>") |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpectedDlls = @($dllNames | Where-Object {
    $_ -notin @("KERNEL32.DLL", "USER32.DLL", "GDI32.DLL")
})
if ($unexpectedDlls.Count -ne 0 -or
    $dumpText -match "GetCommandLineW|GetModuleFileNameW|__CHK") {
    throw "The trace dump tool contains an incompatible runtime import."
}

Write-Output "Built Velocity9x HAL trace dump tool: $executable"
Write-Output "Verified runtime-free imports: $($dllNames -join ', ')"
