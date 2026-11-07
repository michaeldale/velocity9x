[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = 'C:\98DDK'
)

# CKMM.VXD + V9XCKMM.EXE: the read-only AMD Mullins (Radeon R3E, GCN 2)
# register snapshot, Phase 2 of docs\plans\ati-mullins-gfx7.md. A copy of
# build-carrizo-mmio-probe.ps1 for the CIK copy of that probe.

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot 'build\cik-mmio-probe'

. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'ckmm-local'
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw 'BuildId may contain only letters, digits, dot, underscore, plus, and hyphen.'
}

$assembler = Join-Path $DdkRoot 'bin\win98\ML.EXE'
$vxdLinker = Join-Path $DdkRoot 'bin\LINK.EXE'
$ddkInclude = Join-Path $DdkRoot 'inc\win98'
$required = @($assembler, $vxdLinker, (Join-Path $ddkInclude 'VMM.INC'),
              (Join-Path $ddkInclude 'VWIN32.INC'))
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count -ne 0) {
    throw "Required Windows 98 DDK inputs are missing: $($missing -join ', ')"
}

$watcomRoot = $env:WATCOM
if (-not $watcomRoot -and (Test-Path -LiteralPath 'C:\WATCOM')) {
    $watcomRoot = 'C:\WATCOM'
}
if (-not $watcomRoot) {
    throw 'Open Watcom was not found. Set WATCOM or install it at C:\WATCOM.'
}
$compiler = Join-Path $watcomRoot 'binnt64\wcc386.exe'
$linker = Join-Path $watcomRoot 'binnt64\wlink.exe'
$dumper = Join-Path $watcomRoot 'binnt64\wdump.exe'
$libraries = @((Join-Path $watcomRoot 'lib386\nt\kernel32.lib'))
$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

# The VxD.
$definition = Join-Path $outputDir 'ckmm.def'
$vxdObject = Join-Path $outputDir 'cik_mmio_probe.obj'
$vxdPath = Join-Path $outputDir 'ckmm.vxd'
$vxdMap = Join-Path $outputDir 'ckmm.map'
Set-Content -LiteralPath $definition -Encoding Ascii -Value @(
    'VXD CKMM DYNAMIC',
    "DESCRIPTION 'Velocity9x Mullins read-only register probe'",
    'SEGMENTS',
    "    _LTEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _LDATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _TEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _DATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    CONST CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _BSS CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    'EXPORTS',
    '    CKMM_DDB @1'
)
$vxdSource = Join-Path $repoRoot 'tools\diag\cik_mmio_probe.asm'
& $assembler '-coff' '-DBLD_COFF' '-W2' '-Zd' '-c' '-Cx' '-DMASM6' '-Sg' `
    "-I$ddkInclude" "-Fo$vxdObject" $vxdSource
if ($LASTEXITCODE -ne 0) {
    throw 'The Windows 98 DDK assembler failed to build CKMM.VXD.'
}
& $vxdLinker '/VXD' '/NOD' $vxdObject '/IGNORE:4078' '/IGNORE:4039' `
    "/OUT:$vxdPath" "/MAP:$vxdMap" "/DEF:$definition"
if ($LASTEXITCODE -ne 0) {
    throw 'The Windows 98 DDK linker failed to create CKMM.VXD.'
}
$vxdBytes = [IO.File]::ReadAllBytes($vxdPath)
$le = if ($vxdBytes.Length -ge 64) { [BitConverter]::ToInt32($vxdBytes, 0x3c) } else { -1 }
if ($le -lt 0 -or $le + 1 -ge $vxdBytes.Length -or
    $vxdBytes[0] -ne 0x4d -or $vxdBytes[1] -ne 0x5a -or
    $vxdBytes[$le] -ne 0x4c -or $vxdBytes[$le + 1] -ne 0x45) {
    throw 'CKMM.VXD is not an MZ/LE image.'
}
if (-not [Text.Encoding]::ASCII.GetString($vxdBytes).Contains('CKMM_DDB')) {
    throw 'CKMM.VXD is missing its exported device descriptor.'
}

# The publisher.
$sources = @(
    (Join-Path $repoRoot 'tools\diag\cik_mmio_probe_win32.c')
)
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $outputDir ([IO.Path]::GetFileNameWithoutExtension($source) + '.obj')
    & $compiler '-bt=nt' '-zq' '-wx' '-we' '-zl' '-s' `
        "-i=$(Join-Path $repoRoot 'include')" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to compile $source."
    }
    $objects += $object
}
$executable = Join-Path $outputDir 'v9xckmm.exe'
$mapFile = Join-Path $outputDir 'v9xckmm.map'
$linkFile = Join-Path $outputDir 'v9xckmm.lnk'
$linkLines = @(
    'format windows nt',
    'runtime windows=4.0',
    'option quiet',
    'option nodefaultlibs',
    "option start='_V9xMullinsMmioEntry@0'",
    'option stack=65536',
    "option map='$mapFile'",
    "name '$executable'"
)
$linkLines += $objects | ForEach-Object { "file '$_'" }
$linkLines += $libraries | ForEach-Object { "library '$_'" }
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) {
    throw 'Open Watcom failed to link V9XCKMM.EXE.'
}
Add-V9xVersionResource -RepoRoot $repoRoot -WatcomRoot $watcomRoot -Image $executable `
    -BuildId $BuildId -FileDescription 'Velocity9x Mullins register snapshot' -Kind app

$dumpText = (@(& $dumper -e $executable 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw 'Open Watcom could not inspect V9XCKMM.EXE.'
}
$dllNames = [regex]::Matches($dumpText, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpected = @($dllNames | Where-Object { $_ -notin @('KERNEL32.DLL') })
if ($unexpected.Count -ne 0 -or $dumpText -match 'GetCommandLineW|__CHK') {
    throw 'V9XCKMM.EXE contains an incompatible runtime import.'
}

Write-Output "Built Mullins register probe VxD: $vxdPath"
Write-Output "Built Mullins register probe publisher: $executable"
