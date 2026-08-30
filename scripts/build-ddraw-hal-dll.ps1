[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\ddraw-hal"
$imageBase = [uint32]::Parse("B0400000",
    [System.Globalization.NumberStyles]::HexNumber)

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "ddraw-hal-local"
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
$kernel32 = Join-Path $watcomRoot "lib386\nt\kernel32.lib"
$missingInputs = @(@($compiler, $linker, $dumper, $kernel32) |
    Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missingInputs.Count -ne 0) {
    throw "Required DirectDraw HAL inputs are missing: $($missingInputs -join ', ')"
}

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$includeDir = Join-Path $repoRoot "include"
# The HAL's own private header lives beside its sources, so every translation
# unit can say #include "ddhal_internal.h" regardless of which subdirectory it
# sits in.
$privateIncludeDir = Join-Path $repoRoot "src\display32"
# One entry per translation unit. Object names are flattened into one output
# directory, so two sources may not share a base name.
$sources = @(
    "src\display32\ddhal_core.c",
    "src\display32\blt_cpu.c",
    "src\display32\engines\vga_scanout.c",
    "src\display32\engines\eng_s3_virge.c",
    "src\display32\engines\eng_s3_trio.c",
    # The D3D core before its engines, matching the 2D order above: the core
    # owns the entry points and the engine selector, an engine owns registers.
    "src\display32\d3d\d3d_core.c",
    # The 1.31 depth conversion, kept in its own translation unit so the host
    # build can compile and test it without the DDHAL around it.
    "src\display32\d3d\d3d_zfixed.c",
    "src\display32\d3d\d3d_virge.c",
    "src\display32\d3d\d3d_soft.c"
)
$dll = Join-Path $outputDir "v9xhal.dll"
$mapFile = Join-Path $outputDir "v9xhal.map"
$linkFile = Join-Path $outputDir "v9xhal.lnk"

$objects = @()
$objectNames = @{}
foreach ($relative in $sources) {
    $source = Join-Path $repoRoot $relative
    if (-not (Test-Path -LiteralPath $source)) {
        throw "DirectDraw HAL source is missing: $relative"
    }
    $name = [IO.Path]::GetFileNameWithoutExtension($source)
    if ($objectNames.ContainsKey($name)) {
        throw ("Two DirectDraw HAL sources share the base name '$name': " +
               "$($objectNames[$name]) and $relative.")
    }
    $objectNames[$name] = $relative
    $object = Join-Path $outputDir "$name.obj"
    & $compiler "-bt=nt" "-bd" "-zq" "-wx" "-we" "-zl" "-s" `
        "-i=$includeDir" "-i=$privateIncludeDir" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to compile $relative."
    }
    $objects += $object
}

$linkLines = @(
    "format windows nt dll",
    "runtime windows=4.0",
    "option quiet",
    "option nodefaultlibs",
    "option start='_V9xHalEntry@12'",
    "alias '__DLLstart_'='_V9xHalEntry@12'",
    ("option offset=0x{0:X8}" -f $imageBase),
    "option map='$mapFile'",
    "option modname='V9XHAL'",
    "export DriverInit='_DriverInit@4'",
    "name '$dll'"
) + @($objects | ForEach-Object { "file '$_'" }) + @(
    "library '$kernel32'"
)
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link the DirectDraw HAL DLL."
}

$bytes = [System.IO.File]::ReadAllBytes($dll)
if ($bytes.Length -lt 0x100 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "The DirectDraw HAL output is not an MZ image."
}
$peOffset = [System.BitConverter]::ToInt32($bytes, 0x3c)
if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45) {
    throw "The DirectDraw HAL output is not a PE image."
}
$sectionCount = [System.BitConverter]::ToUInt16($bytes, $peOffset + 6)
$optionalHeaderSize = [System.BitConverter]::ToUInt16($bytes, $peOffset + 20)
$optionalHeader = $peOffset + 24
$magic = [System.BitConverter]::ToUInt16($bytes, $optionalHeader)
if ($magic -ne 0x10b) {
    throw "The DirectDraw HAL is not a PE32 image."
}
$actualBase = [System.BitConverter]::ToUInt32($bytes, $optionalHeader + 28)
if ($actualBase -ne [uint32]$imageBase) {
    throw ("The DirectDraw HAL image base is 0x{0:X8}, expected 0x{1:X8}." `
        -f $actualBase, [uint32]$imageBase)
}

# Mark every section shared so the image behaves as one global instance in
# the Win9x shared arena (IMAGE_SCN_MEM_SHARED = 0x10000000).
$sectionTable = $optionalHeader + $optionalHeaderSize
$sharedNames = @()
for ($index = 0; $index -lt $sectionCount; $index++) {
    $section = $sectionTable + $index * 40
    $name = ([System.Text.Encoding]::ASCII.GetString($bytes, $section, 8)).TrimEnd([char]0)
    $flags = [System.BitConverter]::ToUInt32($bytes, $section + 36)
    $flags = $flags -bor 0x10000000
    [Array]::Copy([System.BitConverter]::GetBytes($flags), 0, $bytes, $section + 36, 4)
    $sharedNames += $name
}
[System.IO.File]::WriteAllBytes($dll, $bytes)

$dumpText = (@(& $dumper -e $dll 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom could not inspect the DirectDraw HAL DLL."
}
if ($dumpText -notmatch "DriverInit") {
    throw "The DirectDraw HAL DLL does not export DriverInit."
}
$dllNames = [regex]::Matches($dumpText, "DLL name = <([^>]+)>") |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpectedDlls = @($dllNames | Where-Object { $_ -notin @("KERNEL32.DLL") })
if ($unexpectedDlls.Count -ne 0 -or
    $dumpText -match "GetCommandLineW|GetModuleFileNameW|__CHK") {
    throw "The DirectDraw HAL DLL contains an incompatible runtime import."
}
foreach ($requiredApi in @("CloseHandle", "CreateFileA", "FlushFileBuffers",
                            "SetUnhandledExceptionFilter", "WriteFile")) {
    if ($dumpText -notmatch [regex]::Escape($requiredApi)) {
        throw "The DirectDraw HAL DLL is missing import $requiredApi."
    }
}
$imageText = [System.Text.Encoding]::ASCII.GetString($bytes)
if (-not $imageText.Contains($BuildId)) {
    throw "The DirectDraw HAL DLL is missing the build identifier."
}

Write-Output "Built DirectDraw HAL DLL: $dll"
Write-Output ("Image base 0x{0:X8}; shared sections: {1}" `
    -f [uint32]$imageBase, ($sharedNames -join ', '))
Write-Output "Verified imports: $(if ($dllNames) { $dllNames -join ', ' } else { 'none' })"
