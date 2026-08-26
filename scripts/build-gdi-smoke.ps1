[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\gdi-smoke"

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "gdi-smoke-local"
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
    (Join-Path $watcomRoot "lib386\nt\gdi32.lib")
)
$missingInputs = @(@($compiler, $linker, $dumper) + $libraries |
    Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missingInputs.Count -ne 0) {
    throw "Required GDI smoke-test inputs are missing: $($missingInputs -join ', ')"
}

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$source = Join-Path $repoRoot "tools\diag\gdi_smoke_win32.c"
$object = Join-Path $outputDir "gdi_smoke_win32.obj"
$executable = Join-Path $outputDir "v9xgdi.exe"
$mapFile = Join-Path $outputDir "v9xgdi.map"
$linkFile = Join-Path $outputDir "v9xgdi.lnk"

# The /accel phase reads the driver's GDI counters through the project-private
# V9X_GDIGETSTATS escape, whose struct and command ids live in
# include\velocity9x\win9x_ddraw_abi.h.
$includeDir = Join-Path $repoRoot "include"
& $compiler "-bt=nt" "-zq" "-wx" "-zl" "-s" `
    "-i=$includeDir" "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to compile the GDI framebuffer smoke test."
}

$linkLines = @(
    "format windows nt",
    "runtime windows=4.0",
    "option quiet",
    "option nodefaultlibs",
    "option start='_V9xGdiSmokeEntry@0'",
    "option stack=65536",
    "option map='$mapFile'",
    "name '$executable'",
    "file '$object'"
)
$linkLines += $libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link the GDI framebuffer smoke test."
}

$bytes = [System.IO.File]::ReadAllBytes($executable)
$newHeaderOffset = if ($bytes.Length -ge 64) {
    [System.BitConverter]::ToInt32($bytes, 0x3c)
} else { -1 }
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or
    $bytes[1] -ne 0x5a -or $newHeaderOffset -lt 0 -or
    $newHeaderOffset + 1 -ge $bytes.Length -or
    $bytes[$newHeaderOffset] -ne 0x50 -or
    $bytes[$newHeaderOffset + 1] -ne 0x45) {
    throw "The GDI smoke test is not an MZ/PE executable."
}
$imageText = [System.Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @($BuildId, "Velocity9x GDI framebuffer test", "PASS:",
                       # The /accel phase and the two strings that make its
                       # failures readable in C:\V9XACCE.INI. The two
                       # "enabled-but-never-fired" messages are the
                       # anti-vacuous-pass check; a build that lost them would
                       # still pass every other assertion in this script.
                       "Velocity9xAccel",
                       "fill-enabled-but-never-fired",
                       "copy-enabled-but-never-fired",
                       "dispatcher-never-called",
                       # A poison this run did not ask for is a failure. These
                       # two are what caught a real Trio64 bounded-wait timeout
                       # that the zero-counter check alone had passed over,
                       # because the counter was already non-zero from an
                       # earlier phase in the same boot.
                       "poisoned-before-run",
                       "poisoned-during-run")) {
    if (-not $imageText.Contains($marker)) {
        throw "The GDI smoke test is missing marker $marker."
    }
}

$dumpText = (@(& $dumper -e $executable 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom could not inspect the GDI smoke test."
}
$dllNames = [regex]::Matches($dumpText, "DLL name = <([^>]+)>") |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpectedDlls = @($dllNames | Where-Object {
    $_ -notin @("KERNEL32.DLL", "USER32.DLL", "GDI32.DLL")
})
foreach ($requiredApi in @("BitBlt", "StretchBlt", "GetPixel", "SetPixel",
                            "TextOutA", "GetCommandLineA",
                            "WritePrivateProfileStringA",
                            # /accel: the reference DC, the readback, and the
                            # escape that reads the driver's own counters.
                            # /accel draws its reference at the screen's own
                            # pixel format (CreateCompatibleBitmap) and reads
                            # both sides back at 24 bpp (GetDIBits). A 24-bpp
                            # reference is wrong and was measured wrong: see the
                            # comment on v9x_accel_dib_bytes.
                            "PatBlt", "CreateCompatibleBitmap", "GetDIBits",
                            "ExtEscape", "VirtualAlloc")) {
    if ($dumpText -notmatch "(?m)\s$([regex]::Escape($requiredApi))\s*$") {
        throw "The GDI smoke test is missing import $requiredApi."
    }
}
if ($unexpectedDlls.Count -ne 0 -or
    $dumpText -match "GetCommandLineW|GetModuleFileNameW|__CHK") {
    throw "The GDI smoke test contains an incompatible runtime import."
}

Write-Output "Built Windows 98 GDI framebuffer smoke test: $executable"
Write-Output "Verified runtime-free imports: $($dllNames -join ', ')"
