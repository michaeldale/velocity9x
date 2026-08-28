# Build the DOS-box hang test driver handed to whoever has the affected
# machine in front of them.
#
# See docs\issues\2026-08-28-dos-box-entry-hang-gma950.md. The machine cannot
# be traced - no boot stage is written on that path and the netbook has no
# serial port - so the tool's whole job is to write a record before the risky
# action and read it back after the power cycle. The output folder is the
# distribution: the executable plus the instructions.
[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\dos-box-test"

. (Join-Path $PSScriptRoot "common.ps1")
$productVersion = Get-V9xProductVersion -RepoRoot $repoRoot
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "dos-box-test-local"
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw "BuildId may contain only letters, digits, dot, underscore, plus, and hyphen."
}

$watcomRoot = $env:WATCOM
if (-not $watcomRoot -and (Test-Path -LiteralPath "C:\WATCOM")) {
    $watcomRoot = "C:\WATCOM"
}
if (-not $watcomRoot) {
    throw "Open Watcom was not found. Set WATCOM or install it at C:\WATCOM."
}

$compiler = Join-Path $watcomRoot "binnt64\wcc386.exe"
$linker = Join-Path $watcomRoot "binnt64\wlink.exe"
$dumper = Join-Path $watcomRoot "binnt64\wdump.exe"
$libraries = @(
    (Join-Path $watcomRoot "lib386\nt\kernel32.lib"),
    (Join-Path $watcomRoot "lib386\nt\user32.lib"),
    # GetDeviceCaps only, to record the desktop mode each trial ran under.
    (Join-Path $watcomRoot "lib386\nt\gdi32.lib")
)
$missingInputs = @(@($compiler, $linker, $dumper) + $libraries |
    Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missingInputs.Count -ne 0) {
    throw "Required dos-box-test inputs are missing: $($missingInputs -join ', ')"
}

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$source = Join-Path $repoRoot "tools\diag\dos_box_test_win32.c"
$object = Join-Path $outputDir "dos_box_test_win32.obj"
$executable = Join-Path $outputDir "V9XDOSBX.EXE"
$mapFile = Join-Path $outputDir "v9xdosbx.map"
$linkFile = Join-Path $outputDir "v9xdosbx.lnk"

& $compiler "-bt=nt" "-zq" "-wx" "-zl" "-s" `
    "-i=$(Join-Path $repoRoot 'include')" `
    "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to compile the DOS-box test driver."
}

$linkLines = @(
    "format windows nt",
    "runtime windows=4.0",
    "option quiet",
    "option nodefaultlibs",
    "option start='_V9xDosBoxTestEntry@0'",
    "option stack=65536",
    "option map='$mapFile'",
    "name '$executable'",
    "file '$object'"
)
$linkLines += $libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link the DOS-box test driver."
}

# The same runtime-free import audit every Win32 diagnostic gets: this one runs
# on Windows 98 with no redistributable, and a wide-character or stack-check
# import would fail at load with a message that reads like the defect.
$dumpText = (@(& $dumper -e $executable 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom could not inspect the DOS-box test driver."
}
$dllNames = [regex]::Matches($dumpText, "DLL name = <([^>]+)>") |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpectedDlls = @($dllNames | Where-Object {
    $_ -notin @("KERNEL32.DLL", "USER32.DLL", "GDI32.DLL")
})
if ($unexpectedDlls.Count -ne 0 -or
    $dumpText -match "GetCommandLineW|GetModuleFileNameW|__CHK") {
    throw "The DOS-box test driver contains an incompatible runtime import."
}

$readme = @(
    "VELOCITY9X DOS BOX TEST",
    "Build: $BuildId ($productVersion)",
    "",
    "WHAT THIS IS",
    "",
    "On some machines a DOS box hangs Windows outright: the screen goes to a",
    "logo or a blank, and nothing comes back. This program finds out which",
    "cases do it and which do not.",
    "",
    "It cannot be done with a batch file, because running one opens a DOS box",
    "- the thing being tested.",
    "",
    "",
    "HOW TO RUN IT",
    "",
    "   1. Copy V9XDOSBX.EXE anywhere on the machine and double-click it.",
    "   2. Read the box it shows, press OK, and do what it says.",
    "   3. Run it again. Repeat.",
    "",
    "It alternates a windowed DOS box and a full screen one. Once you have",
    "done both, change the colour depth or the resolution in Display",
    "Properties and run it again - it records the mode each trial ran under,",
    "so the picture builds up on its own.",
    "",
    "",
    "IF THE MACHINE HANGS",
    "",
    "That is a result, not a mistake. Power the machine off, start it again,",
    "and run the program once more. It writes its record before it opens the",
    "box, so it knows on the next run that the last one never came back.",
    "There is nothing to write down and nothing to remember.",
    "",
    "",
    "WHAT TO SEND",
    "",
    "C:\\V9XDIAG\\V9XDOSBX.INI - the whole file. It holds one line per trial:",
    "the mode, whether the box was windowed or full screen, and whether the",
    "machine survived it.",
    "",
    "Send C:\\V9XDIAG\\V9XBOOT.INI too if it is there.",
    "",
    "",
    "WHAT IT CHANGES",
    "",
    "Nothing. It writes one file in C:\\V9XDIAG, starts your command",
    "interpreter the way Start/Run does, and waits for it to close. It sets no",
    "video mode, installs nothing, and touches no hardware register.",
    "",
    "",
    "SWITCHES",
    "",
    "Normally none - just double-click it. /win forces a windowed trial and",
    "/full forces a full screen one, for repeating a single case."
)
Set-Content -LiteralPath (Join-Path $outputDir "README.TXT") `
    -Encoding Ascii -Value $readme

$hashLines = Get-ChildItem -LiteralPath $outputDir -File |
    Where-Object { $_.Extension -notin @(".obj", ".map", ".lnk") -and
                   $_.Name -ne "SHA256.TXT" } |
    Sort-Object Name |
    ForEach-Object {
        "$((Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash)  $($_.Name)"
    }
Set-Content -LiteralPath (Join-Path $outputDir "SHA256.TXT") `
    -Encoding Ascii -Value $hashLines

Write-Output "Built Velocity9x DOS box test: $executable"
Write-Output "Verified runtime-free imports: $($dllNames -join ', ')"
Write-Output "Distribution folder: $outputDir"
