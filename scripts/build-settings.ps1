[CmdletBinding()]
param(
    [string]$BuildId,
    # The family's Package.ModesSummary; see build-settings-page.ps1, which
    # compiles the same settings_status.c and takes it for the same reason.
    [string]$ModesSummary
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\settings"

. (Join-Path $PSScriptRoot "common.ps1")
$ProductVersion = Get-V9xProductVersion -RepoRoot $repoRoot
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "settings-local"
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
$resourceCompiler = Join-Path $watcomRoot "binnt64\wrc.exe"
$libraries = @(
    (Join-Path $watcomRoot "lib386\nt\kernel32.lib"),
    (Join-Path $watcomRoot "lib386\nt\user32.lib"),
    (Join-Path $watcomRoot "lib386\nt\gdi32.lib"),
    (Join-Path $watcomRoot "lib386\nt\shell32.lib")
)
$logoSource = Join-Path $repoRoot "logo\velocity9x-logo-concept.png"
$missingInputs = @(@($compiler, $linker, $dumper, $resourceCompiler,
                     $logoSource) + $libraries |
    Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missingInputs.Count -ne 0) {
    throw "Required settings build inputs are missing: $($missingInputs -join ', ')"
}

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$diagDir = Join-Path $repoRoot "tools\diag"
$includeDir = Join-Path $repoRoot "include"
$sources = @(
    (Join-Path $diagDir "settings_win32.c"),
    (Join-Path $diagDir "settings_status.c")
)
$executable = Join-Path $outputDir "v9xset.exe"
$mapFile = Join-Path $outputDir "v9xset.map"
$linkFile = Join-Path $outputDir "v9xset.lnk"
$logoBitmap = Join-Path $outputDir "velocity9x-logo-settings.bmp"
$resourceFile = Join-Path $outputDir "settings.rc"

Add-Type -AssemblyName System.Drawing
$sourceImage = [Drawing.Image]::FromFile($logoSource)
try {
    $bitmap = New-Object Drawing.Bitmap 386, 54,
        ([Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([Drawing.Color]::White)
            $graphics.InterpolationMode =
                [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $destination = New-Object Drawing.Rectangle 0, 0, 386, 54
            $sourceRectangle = New-Object Drawing.Rectangle 45, 300, 1680, 340
            $graphics.DrawImage($sourceImage, $destination, $sourceRectangle,
                                [Drawing.GraphicsUnit]::Pixel)
        } finally { $graphics.Dispose() }
        $bitmap.Save($logoBitmap, [Drawing.Imaging.ImageFormat]::Bmp)
    } finally { $bitmap.Dispose() }
} finally { $sourceImage.Dispose() }
Set-Content -LiteralPath $resourceFile -Encoding Ascii -Value (
    '101 BITMAP "{0}"' -f $logoBitmap.Replace('\', '\\'))

$modeDefines = @()
if ($ModesSummary) {
    if ($ModesSummary -match '["\\]') {
        throw "ModesSummary may not contain quotes or backslashes."
    }
    $modeDefines += "-dV9X_MODES_SUMMARY=`"$ModesSummary`""
}

$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $outputDir (
        [IO.Path]::GetFileNameWithoutExtension($source) + ".obj")
    & $compiler "-bt=nt" "-zq" "-wx" "-zl" "-s" `
        "-i=$diagDir" "-i=$includeDir" `
        "-dV9X_BUILD_ID=`"$BuildId`"" @modeDefines "-fo=$object" $source
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
    "option start='_V9xSettingsEntry@0'",
    "option stack=65536",
    "option map='$mapFile'",
    "name '$executable'"
)
$linkLines += $objects | ForEach-Object { "file '$_'" }
$linkLines += $libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link the runtime-free settings utility."
}
& $resourceCompiler "-q" "-bt=nt" $resourceFile $executable
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to embed the Velocity9x logo resource."
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
    throw "The settings utility is not an MZ/PE executable."
}
$imageText = [System.Text.Encoding]::ASCII.GetString($bytes)
# The mode-list marker follows whatever was compiled in, so the check still
# proves the supported-modes line reached the image rather than proving that one
# particular resolution is spelled in the source.
$modesMarker = if ($ModesSummary) { $ModesSummary }
               else { "see Display Properties, Settings" }
foreach ($marker in @($BuildId, "Version: $ProductVersion", "Velocity9x Settings",
                      $modesMarker,
                      "Core / engine clock",
                       "Run GDI test")) {
    if (-not $imageText.Contains($marker)) {
        throw "The settings utility is missing marker $marker."
    }
}

$dumpText = (@(& $dumper -e $executable 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom could not inspect the settings utility."
}
$resourceDump = (@(& $dumper -r $executable 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0 -or $resourceDump -notmatch '(?i)BITMAP') {
    throw "The settings utility is missing its embedded logo bitmap."
}
$dllNames = [regex]::Matches($dumpText, "DLL name = <([^>]+)>") |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpectedDlls = @($dllNames | Where-Object {
    $_ -notin @("KERNEL32.DLL", "USER32.DLL", "GDI32.DLL", "SHELL32.DLL")
})
if ($unexpectedDlls.Count -ne 0 -or
    $dumpText -match "GetCommandLineW|GetModuleFileNameW|__CHK") {
    throw "The settings utility contains an incompatible runtime import."
}

Write-Output "Built Windows 98 settings and diagnostics utility: $executable"
Write-Output "Verified runtime-free imports: $($dllNames -join ', ')"
