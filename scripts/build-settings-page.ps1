[CmdletBinding()]
param(
    [string]$BuildId
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\settings-page"

. (Join-Path $PSScriptRoot "common.ps1")
$ProductVersion = Get-V9xProductVersion -RepoRoot $repoRoot
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "settings-page-local"
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
    (Join-Path $watcomRoot "lib386\nt\comctl32.lib"),
    # The page registers itself through RunOnce, which needs the registry API.
    (Join-Path $watcomRoot "lib386\nt\advapi32.lib")
)
$logoSource = Join-Path $repoRoot "logo\velocity9x-logo-concept.png"
$missingInputs = @(@($compiler, $linker, $dumper, $resourceCompiler,
                     $logoSource) + $libraries |
    Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missingInputs.Count -ne 0) {
    throw "Required settings-page build inputs are missing: $($missingInputs -join ', ')"
}

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$diagDir = Join-Path $repoRoot "tools\diag"
$includeDir = Join-Path $repoRoot "include"
$sources = @(
    (Join-Path $diagDir "settings_propsheet.c"),
    (Join-Path $diagDir "settings_status.c")
)
$objects = @()
$library = Join-Path $outputDir "v9xsetp.dll"
$mapFile = Join-Path $outputDir "v9xsetp.map"
$linkFile = Join-Path $outputDir "v9xsetp.lnk"
$logoBitmap = Join-Path $outputDir "velocity9x-logo-settings.bmp"
$resourceSource = Join-Path $diagDir "settings_propsheet.rc"
$resourceFile = Join-Path $outputDir "settings_propsheet.rc"

Add-Type -AssemblyName System.Drawing
$sourceImage = [Drawing.Image]::FromFile($logoSource)
try {
    # Keep this smaller than the dialog's logo slot in both axes. The static
    # control uses SS_CENTERIMAGE, which clips anything larger than itself
    # rather than scaling it; at 355x71 the bitmap was taller than the slot and
    # lost its top and bottom edges. The slot is deliberately modest because
    # the page height sets the size of the whole Display Properties dialog and
    # it has to fit a 640x480 screen. See tools/diag/settings_propsheet.rc.
    $bitmap = New-Object Drawing.Bitmap 185, 37,
        ([Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([Drawing.Color]::White)
            $graphics.InterpolationMode =
                [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $destination = New-Object Drawing.Rectangle 0, 0, 185, 37
            $sourceRectangle = New-Object Drawing.Rectangle 45, 300, 1680, 340
            $graphics.DrawImage($sourceImage, $destination, $sourceRectangle,
                                [Drawing.GraphicsUnit]::Pixel)
        } finally { $graphics.Dispose() }
        $bitmap.Save($logoBitmap, [Drawing.Imaging.ImageFormat]::Bmp)
    } finally { $bitmap.Dispose() }
} finally { $sourceImage.Dispose() }

$resourceText = Get-Content -LiteralPath $resourceSource -Raw
$resourceText += "`r`n101 BITMAP `"{0}`"`r`n" -f $logoBitmap.Replace('\', '\\')
Set-Content -LiteralPath $resourceFile -Encoding Ascii -Value $resourceText

foreach ($source in $sources) {
    $object = Join-Path $outputDir (
        [IO.Path]::GetFileNameWithoutExtension($source) + ".obj")
    & $compiler "-bt=nt" "-bd" "-zq" "-wx" "-zl" "-s" `
        "-i=$diagDir" "-i=$includeDir" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to compile $source."
    }
    $objects += $object
}

$linkLines = @(
    "format windows nt dll",
    "runtime windows=4.0",
    "option quiet",
    "option nodefaultlibs",
    "option start='_V9xPageEntry@12'",
    "alias '__DLLstart_'='_V9xPageEntry@12'",
    "option map='$mapFile'",
    "option modname='V9XSETP'",
    "export DllGetClassObject='_DllGetClassObject@12'",
    "export DllCanUnloadNow='_DllCanUnloadNow@0'",
    # Undecorated, because rundll32 looks the name up exactly as the INF's
    # RunOnce command line spells it.
    "export V9xRegisterPage='_V9xRegisterPage@16'",
    "name '$library'"
)
$linkLines += $objects | ForEach-Object { "file '$_'" }
$linkLines += $libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to link the settings property-sheet DLL."
}
& $resourceCompiler "-q" "-bt=nt" "-i=$diagDir" $resourceFile $library
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom failed to embed the settings-page resources."
}

$bytes = [System.IO.File]::ReadAllBytes($library)
$newHeaderOffset = if ($bytes.Length -ge 64) {
    [System.BitConverter]::ToInt32($bytes, 0x3c)
} else { -1 }
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or
    $bytes[1] -ne 0x5a -or $newHeaderOffset -lt 0 -or
    $newHeaderOffset + 1 -ge $bytes.Length -or
    $bytes[$newHeaderOffset] -ne 0x50 -or
    $bytes[$newHeaderOffset + 1] -ne 0x45) {
    throw "The settings page is not an MZ/PE image."
}
$imageText = [System.Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @($BuildId, "Version: $ProductVersion", "Velocity9x Settings",
                      "Velocity9x settings report")) {
    if (-not $imageText.Contains($marker)) {
        throw "The settings page is missing marker $marker."
    }
}

$dumpText = (@(& $dumper -e $library 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Open Watcom could not inspect the settings page."
}
foreach ($export in @("DllGetClassObject", "DllCanUnloadNow",
                      "V9xRegisterPage")) {
    if ($dumpText -notmatch [regex]::Escape($export)) {
        throw "The settings page does not export $export."
    }
}
# PE resources are listed by numeric type: 2 = RT_BITMAP id 101 (0x65),
# 5 = RT_DIALOG id 2000 (0x7D0).
$resourceDump = (@(& $dumper -r $library 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0 -or
    $resourceDump -notmatch '(?m)^00000002\s+00000065' -or
    $resourceDump -notmatch '(?m)^00000005\s+000007D0') {
    throw "The settings page is missing its dialog or logo resource."
}
$dllNames = [regex]::Matches($dumpText, "DLL name = <([^>]+)>") |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpectedDlls = @($dllNames | Where-Object {
    $_ -notin @("KERNEL32.DLL", "USER32.DLL", "GDI32.DLL", "COMCTL32.DLL",
                "ADVAPI32.DLL")
})
if ($unexpectedDlls.Count -ne 0 -or
    $dumpText -match "GetCommandLineW|GetModuleFileNameW|__CHK") {
    throw "The settings page contains an incompatible runtime import."
}

Write-Output "Built Display Properties settings page: $library"
Write-Output "Verified runtime-free imports: $($dllNames -join ', ')"
