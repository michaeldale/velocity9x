[CmdletBinding()]
param(
    [string]$DdkRoot = 'C:\98DDK'
)

# PFRM.VXD: the caller's page-table entries for a linear range, which
# V9XD32's T15 uses to name the physical frame behind a bad word. Built as
# build-r128-mmio-probe.ps1 builds R1MM.VXD.

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot 'build\page-frames'

$assembler = Join-Path $DdkRoot 'bin\win98\ML.EXE'
$vxdLinker = Join-Path $DdkRoot 'bin\LINK.EXE'
$ddkInclude = Join-Path $DdkRoot 'inc\win98'
$required = @($assembler, $vxdLinker, (Join-Path $ddkInclude 'VMM.INC'),
              (Join-Path $ddkInclude 'VWIN32.INC'))
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count -ne 0) {
    throw "Required Windows 98 DDK inputs are missing: $($missing -join ', ')"
}
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$definition = Join-Path $outputDir 'pfrm.def'
$vxdObject = Join-Path $outputDir 'page_frames.obj'
$vxdPath = Join-Path $outputDir 'PFRM.VXD'
$vxdMap = Join-Path $outputDir 'pfrm.map'
Set-Content -LiteralPath $definition -Encoding Ascii -Value @(
    'VXD PFRM DYNAMIC',
    "DESCRIPTION 'Velocity9x page-table copy for V9XD32'",
    'SEGMENTS',
    "    _LTEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _LDATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _TEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _DATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    CONST CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _BSS CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    'EXPORTS',
    '    PFRM_DDB @1'
)
$vxdSource = Join-Path $repoRoot 'tools\diag\page_frames.asm'
& $assembler '-coff' '-DBLD_COFF' '-W2' '-Zd' '-c' '-Cx' '-DMASM6' '-Sg' `
    "-I$ddkInclude" "-Fo$vxdObject" $vxdSource
if ($LASTEXITCODE -ne 0) {
    throw 'The Windows 98 DDK assembler failed to build PFRM.VXD.'
}
& $vxdLinker '/VXD' '/NOD' $vxdObject '/IGNORE:4078' '/IGNORE:4039' `
    "/OUT:$vxdPath" "/MAP:$vxdMap" "/DEF:$definition"
if ($LASTEXITCODE -ne 0) {
    throw 'The Windows 98 DDK linker failed to create PFRM.VXD.'
}
$vxdBytes = [IO.File]::ReadAllBytes($vxdPath)
$le = if ($vxdBytes.Length -ge 64) { [BitConverter]::ToInt32($vxdBytes, 0x3c) } else { -1 }
if ($le -lt 0 -or $le + 1 -ge $vxdBytes.Length -or
    $vxdBytes[0] -ne 0x4d -or $vxdBytes[1] -ne 0x5a -or
    $vxdBytes[$le] -ne 0x4c -or $vxdBytes[$le + 1] -ne 0x45) {
    throw 'PFRM.VXD is not an MZ/LE image.'
}
if (-not [Text.Encoding]::ASCII.GetString($vxdBytes).Contains('PFRM_DDB')) {
    throw 'PFRM.VXD is missing its exported device descriptor.'
}

Write-Output "Built page-table copy VxD: $vxdPath"
