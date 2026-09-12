[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\mode-switch"

. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "lib\diag-tool.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "mode-switch-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "BuildId may contain only letters, digits, dot, underscore, plus, and hyphen."
}

$source = Join-Path $repoRoot "tools\diag\mode_switch_win32.c"
$object = Join-Path $outputDir "mode_switch_win32.obj"
$executable = Join-Path $outputDir "v9xmsw.exe"

$result = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Object $object -Executable $executable `
    -MapFile (Join-Path $outputDir "v9xmsw.map") `
    -LinkFile (Join-Path $outputDir "v9xmsw.lnk") `
    -LibraryNames @("kernel32.lib", "user32.lib", "gdi32.lib") `
    -CompileArguments @("-bt=nt", "-zq", "-wx", "-zl", "-s",
        "-i=$(Join-Path $repoRoot 'include')",
        "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$object", $source) `
    -LinkOptions @("option start='_V9xModeSwitchEntry@0'", "option stack=65536") `
    -ToolDescription "mode-switch exerciser"

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
    throw "The mode-switch exerciser contains an incompatible runtime import."
}

Write-Output "Built Windows 98 mode-switch exerciser: $executable"
Write-Output "Verified runtime-free imports: $($dllNames -join ', ')"
