[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = 'C:\98DDK'
)

# EGCP.VXD + V9XEGCP.EXE + V9XEG3D.EXE: the AMD Evergreen (Radeon HD 5450,
# Cedar) command processor bring-up probe (Phase 1 of
# docs\plans\ati-evergreen-cedar-3d.md) and the first 3D draw (Phase 2),
# which share the VxD. Built as build-evergreen-dma-probe.ps1 builds the DMA
# probe; each publisher is self-contained and reads AMD's microcode from
# PFP.BIN and ME.BIN beside it at run time.

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot 'build\evergreen-cp-probe'

. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'egcp-local'
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
$definition = Join-Path $outputDir 'egcp.def'
$vxdObject = Join-Path $outputDir 'evergreen_cp.obj'
$vxdPath = Join-Path $outputDir 'egcp.vxd'
$vxdMap = Join-Path $outputDir 'egcp.map'
Set-Content -LiteralPath $definition -Encoding Ascii -Value @(
    'VXD EGCP DYNAMIC',
    "DESCRIPTION 'Velocity9x Evergreen CP bring-up probe'",
    'SEGMENTS',
    "    _LTEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _LDATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _TEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _DATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    CONST CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _BSS CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    'EXPORTS',
    '    EGCP_DDB @1'
)
$vxdSource = Join-Path $repoRoot 'tools\diag\evergreen_cp.asm'
& $assembler '-coff' '-DBLD_COFF' '-W2' '-Zd' '-c' '-Cx' '-DMASM6' '-Sg' `
    "-I$ddkInclude" "-Fo$vxdObject" $vxdSource
if ($LASTEXITCODE -ne 0) {
    throw 'The Windows 98 DDK assembler failed to build EGCP.VXD.'
}
& $vxdLinker '/VXD' '/NOD' $vxdObject '/IGNORE:4078' '/IGNORE:4039' `
    "/OUT:$vxdPath" "/MAP:$vxdMap" "/DEF:$definition"
if ($LASTEXITCODE -ne 0) {
    throw 'The Windows 98 DDK linker failed to create EGCP.VXD.'
}
$vxdBytes = [IO.File]::ReadAllBytes($vxdPath)
$le = if ($vxdBytes.Length -ge 64) { [BitConverter]::ToInt32($vxdBytes, 0x3c) } else { -1 }
if ($le -lt 0 -or $le + 1 -ge $vxdBytes.Length -or
    $vxdBytes[0] -ne 0x4d -or $vxdBytes[1] -ne 0x5a -or
    $vxdBytes[$le] -ne 0x4c -or $vxdBytes[$le + 1] -ne 0x45) {
    throw 'EGCP.VXD is not an MZ/LE image.'
}
if (-not [Text.Encoding]::ASCII.GetString($vxdBytes).Contains('EGCP_DDB')) {
    throw 'EGCP.VXD is missing its exported device descriptor.'
}

# The publishers: the CP bring-up and the first draw.
$publishers = @(
    @{ Source = 'evergreen_cp_win32.c'; Name = 'v9xegcp'; Entry = '_V9xEvergreenCpEntry@0'
       Description = 'Velocity9x Evergreen CP bring-up probe' }
    @{ Source = 'evergreen_3d_win32.c'; Name = 'v9xeg3d'; Entry = '_V9xEvergreen3dEntry@0'
       Description = 'Velocity9x Evergreen first 3D draw probe' }
)
foreach ($publisher in $publishers) {
    $source = Join-Path $repoRoot ('tools\diag\' + $publisher.Source)
    $object = Join-Path $outputDir ([IO.Path]::GetFileNameWithoutExtension($source) + '.obj')
    & $compiler '-bt=nt' '-zq' '-wx' '-we' '-zl' '-s' `
        "-i=$(Join-Path $repoRoot 'include')" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to compile $source."
    }
    $executable = Join-Path $outputDir ($publisher.Name + '.exe')
    $mapFile = Join-Path $outputDir ($publisher.Name + '.map')
    $linkFile = Join-Path $outputDir ($publisher.Name + '.lnk')
    $linkLines = @(
        'format windows nt',
        'runtime windows=4.0',
        'option quiet',
        'option nodefaultlibs',
        "option start='$($publisher.Entry)'",
        'option stack=65536',
        "option map='$mapFile'",
        "name '$executable'",
        "file '$object'"
    )
    $linkLines += $libraries | ForEach-Object { "library '$_'" }
    Set-Content -LiteralPath $linkFile -Encoding Ascii -Value $linkLines
    & $linker "@$linkFile"
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom failed to link $($publisher.Name).exe."
    }
    Add-V9xVersionResource -RepoRoot $repoRoot -WatcomRoot $watcomRoot -Image $executable `
        -BuildId $BuildId -FileDescription $publisher.Description -Kind app

    $dumpText = (@(& $dumper -e $executable 2>&1)) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "Open Watcom could not inspect $($publisher.Name).exe."
    }
    $dllNames = [regex]::Matches($dumpText, 'DLL name = <([^>]+)>') |
        ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
        Sort-Object -Unique
    $unexpected = @($dllNames | Where-Object { $_ -notin @('KERNEL32.DLL') })
    if ($unexpected.Count -ne 0 -or $dumpText -match 'GetCommandLineW|__CHK') {
        throw "$($publisher.Name).exe contains an incompatible runtime import."
    }
    Write-Output "Built $($publisher.Description): $executable"
}

Write-Output "Built Evergreen CP probe VxD: $vxdPath"
