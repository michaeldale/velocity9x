[CmdletBinding()]
param(
    [string]$BuildId,
    [switch]$MatroxMillennium2,
    [ValidateSet(8, 16)]
    [int]$MatroxBitsPerPixel = 8
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot $(if ($MatroxMillennium2) {
    "build\win16-loader-probe-mga2"
} else {
    "build\win16-loader-probe"
})

. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "win16-loader-local"
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

$compiler = Join-Path $watcomRoot "binnt\wcl.exe"
$dumper = Join-Path $watcomRoot "binnt64\wdump.exe"
foreach ($tool in @($compiler, $dumper)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Required Open Watcom tool was not found: $tool"
    }
}

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\win')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$source = Join-Path $repoRoot "tools\diag\win16_driver_loader.c"
$executable = Join-Path $outputDir "v9x16ld.exe"
Push-Location $outputDir
try {
    $arguments = @("-bt=windows", "-l=windows", "-zq", "-wx",
                   "-dV9X_BUILD_ID=`"$BuildId`"", "-fe=$executable")
    if ($MatroxMillennium2) {
        $arguments += "-dV9X_TARGET_MATROX_MILLENNIUM2=1"
        if ($MatroxBitsPerPixel -eq 16) {
            $arguments += "-dV9X_MATROX_16BPP=1"
        }
    }
    $arguments += $source
    & $compiler @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to build the Win16 display-loader probe."
    }
}
finally {
    Pop-Location
}
Add-V9xVersionResource -RepoRoot $repoRoot -WatcomRoot $watcomRoot -Image $executable `
    -BuildId $BuildId -FileDescription "Velocity9x Win16 display driver loader" `
    -Kind app -Win16

$bytes = [System.IO.File]::ReadAllBytes($executable)
$newHeaderOffset = if ($bytes.Length -ge 64) {
    [System.BitConverter]::ToInt32($bytes, 0x3c)
} else { -1 }
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or
    $bytes[1] -ne 0x5a -or $newHeaderOffset -lt 0 -or
    $newHeaderOffset + 1 -ge $bytes.Length -or
    $bytes[$newHeaderOffset] -ne 0x4e -or
    $bytes[$newHeaderOffset + 1] -ne 0x45) {
    throw "The Win16 display-loader probe is not an MZ/NE image."
}
if (-not [System.Text.Encoding]::ASCII.GetString($bytes).Contains($BuildId)) {
    throw "The Win16 display-loader probe does not contain its build identifier."
}

$dumpText = (@(& $dumper -e $executable 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0 -or $dumpText -notmatch "target OS.*=\s+02H" -or
    $dumpText -notmatch "(?m)^\s*KERNEL\s*$" -or
    $dumpText -notmatch "(?m)^\s*USER\s*$") {
    throw "The Win16 display-loader probe failed its Windows import audit."
}

Write-Output "Built Win16 display DRV load/unload utility: $executable"
