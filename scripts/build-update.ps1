# Build V9XUPD.EXE: the update checker, updater and report sender
# (docs\plans\optional-update-checker-and-auto-updater.md).
#
# Several sources and a dialog resource, so this does its own compile, link
# and wrc pass rather than Invoke-V9xDiagToolBuild's single-source one; the
# import audit at the end is the same contract the other tools keep.
[CmdletBinding()]
param(
    [string]$BuildId,
    # Trust the committed test key instead of the release key, for an update
    # cycle against a local fixture signed with it (release_key.h). The build
    # id gains -testkey so such a binary is never mistaken for a shipped one;
    # no package script passes this.
    [switch]$TestKey
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\update"

. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "lib\diag-tool.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "update-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "BuildId may contain only letters, digits, dot, underscore, plus, and hyphen."
}
$keyDefines = @()
if ($TestKey) {
    $BuildId = "$BuildId-testkey"
    $keyDefines = @("-dV9X_RELEASE_TEST_KEY")
}

# ADVAPI32 for the display driver key (V9xFamily, InfSection) and the
# registry half of an update. The network DLLs are loaded at run time and
# must never appear here.
$allowedDlls = @("KERNEL32.DLL", "USER32.DLL", "GDI32.DLL", "ADVAPI32.DLL")
$toolchain = Get-V9xDiagToolchain -Target Win32 -LibraryNames @(
    "kernel32.lib", "user32.lib", "gdi32.lib", "advapi32.lib")
$resourceCompiler = Join-Path (Split-Path -Parent $toolchain.Compiler) "wrc.exe"
if (-not (Test-Path -LiteralPath $resourceCompiler)) {
    throw "Open Watcom wrc.exe was not found beside $($toolchain.Compiler)."
}
$env:WATCOM = $toolchain.WatcomRoot
$env:Path = "$(Join-Path $toolchain.WatcomRoot 'binnt64');$(Join-Path $toolchain.WatcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $toolchain.WatcomRoot 'h');$(Join-Path $toolchain.WatcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$diagDir = Join-Path $repoRoot "tools\diag"
$includeDir = Join-Path $repoRoot "include"
$sources = @(
    (Join-Path $diagDir "update_win32.c"),
    (Join-Path $diagDir "update_net_win32.c"),
    (Join-Path $diagDir "update_install_win32.c")
) + @("update_proto.c", "update_release.c", "update_inf.c", "sha256.c",
      "sha512.c", "ed25519.c", "crc32.c", "inflate.c", "zipread.c" |
    ForEach-Object { Join-Path $repoRoot "src\common\$_" })
$executable = Join-Path $outputDir "v9xupd.exe"
$mapFile = Join-Path $outputDir "v9xupd.map"
$linkFile = Join-Path $outputDir "v9xupd.lnk"
$resourceFile = Join-Path $outputDir "update_all.rc"

$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $outputDir (
        [IO.Path]::GetFileNameWithoutExtension($source) + ".obj")
    & $toolchain.Compiler "-bt=nt" "-zq" "-wx" "-we" "-zl" "-s" `
        "-i=$diagDir" "-i=$includeDir" `
        "-dV9X_BUILD_ID=`"$BuildId`"" @keyDefines "-fo=$object" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to compile $source."
    }
    $objects += $object
}

$linkLines = @(
    "format windows nt",
    "runtime windows=4.0",
    "option quiet",
    "option nodefaultlibs",
    "option start='_V9xUpdateEntry@0'",
    "option stack=65536",
    "option map='$mapFile'",
    "name '$executable'"
)
$linkLines += $objects | ForEach-Object { "file '$_'" }
$linkLines += $toolchain.Libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $toolchain.Linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link V9XUPD.EXE."
}

# wrc replaces an image's resources wholesale, so the dialogs and the
# version resource go in one pass.
Set-Content -LiteralPath $resourceFile -Encoding Ascii -Value (
    ('#include "{0}"' -f (Join-Path $diagDir "update.rc").Replace('\', '\\')) +
    "`r`n" +
    (Get-V9xVersionResourceText -RepoRoot $repoRoot -BuildId $BuildId `
        -FileDescription 'Velocity9x update checker and report sender' `
        -OriginalFilename 'V9XUPD.EXE' -Kind app))
& $resourceCompiler "-q" "-bt=nt" "-i=$diagDir" "-i=$($env:INCLUDE)" `
    $resourceFile $executable
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to add V9XUPD.EXE's resources."
}
Assert-V9xVersionResource -WatcomRoot $toolchain.WatcomRoot -Image $executable

$dumpText = (@(& $toolchain.Dumper -e $executable 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom could not inspect V9XUPD.EXE."
}
$dllNames = [regex]::Matches($dumpText, "DLL name = <([^>]+)>") |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpectedDlls = @($dllNames | Where-Object { $_ -notin $allowedDlls })
if ($unexpectedDlls.Count -ne 0 -or
    $dumpText -match "GetCommandLineW|GetModuleFileNameW|__CHK") {
    throw "V9XUPD.EXE imports $($unexpectedDlls -join ', '); only $($allowedDlls -join ', ') are allowed."
}
$resourceDump = (@(& $toolchain.Dumper -r $executable 2>&1)) -join "`n"
if ($resourceDump -notmatch '(?i)DIALOG') {
    throw "V9XUPD.EXE is missing its dialog resources."
}
$imageText = [System.Text.Encoding]::ASCII.GetString(
    [System.IO.File]::ReadAllBytes($executable))
foreach ($marker in @($BuildId, "V9XUPD/", "/v9update/report",
                      "V9XTRACE.EXE")) {
    if (-not $imageText.Contains($marker)) {
        throw "V9XUPD.EXE is missing marker $marker."
    }
}

Write-Output "Built Velocity9x updater and report sender: $executable"
Write-Output "Verified runtime-free imports: $($dllNames -join ', ')"
