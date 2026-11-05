[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = 'C:\98DDK'
)

# RS3D.VXD + V9XRS3D.EXE: the ATI Radeon X1200 IGP (RS690) 3D engine draw
# probe, Phase 5 of docs\plans\ati-rs690-x1200.md. Same shape as
# build-rs690-2d.ps1; see tools\diag\rs690_3d.asm for what it may write.

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot 'build\rs690-3d'

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'rs3d-local'
}
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') {
    throw 'BuildId may contain only letters, digits, dot, underscore, plus, and hyphen.'
}

$assembler = Join-Path $DdkRoot 'bin\win98\ML.EXE'
$vxdLinker = Join-Path $DdkRoot 'bin\LINK.EXE'
$ddkInclude = Join-Path $DdkRoot 'inc\win98'
$required = @($assembler, $vxdLinker, (Join-Path $ddkInclude 'VMM.INC'),
              (Join-Path $ddkInclude 'MINIVDD.INC'),
              (Join-Path $ddkInclude 'VWIN32.INC'))
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count -ne 0) {
    throw "Required Windows 98 DDK inputs are missing: $($missing -join ', ')"
}

$libraries = @('kernel32.lib', 'gdi32.lib', 'user32.lib', 'advapi32.lib')
$toolchain = Get-V9xDiagToolchain -Target Win32 -LibraryNames $libraries
$env:WATCOM = $toolchain.WatcomRoot
$env:Path = "$(Join-Path $toolchain.WatcomRoot 'binnt64');$(Join-Path $toolchain.WatcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $toolchain.WatcomRoot 'h');$(Join-Path $toolchain.WatcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$definition = Join-Path $outputDir 'rs3d.def'
$vxdObject = Join-Path $outputDir 'rs690_3d.obj'
$vxdPath = Join-Path $outputDir 'rs3d.vxd'
$vxdMap = Join-Path $outputDir 'rs3d.map'
Set-Content -LiteralPath $definition -Encoding Ascii -Value @(
    'VXD RS3D DYNAMIC',
    "DESCRIPTION 'Velocity9x RS690 3D engine draw probe'",
    'SEGMENTS',
    "    _LTEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _LDATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _TEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _DATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    CONST CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _BSS CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    'EXPORTS',
    '    RS3D_DDB @1'
)

$vxdSource = Join-Path $repoRoot 'tools\diag\rs690_3d.asm'
& $assembler '-coff' '-DBLD_COFF' '-W2' '-Zd' '-c' '-Cx' '-DMASM6' '-Sg' `
    '-DVGA' '-DVGA31' '-DMINIVDD=1' "-I$ddkInclude" `
    "-Fo$vxdObject" $vxdSource
if ($LASTEXITCODE -ne 0) {
    throw 'The Windows 98 DDK assembler failed to build rs3d.vxd.'
}
& $vxdLinker '/VXD' '/NOD' $vxdObject '/IGNORE:4078' '/IGNORE:4039' `
    "/OUT:$vxdPath" "/MAP:$vxdMap" "/DEF:$definition"
if ($LASTEXITCODE -ne 0) {
    throw 'The Windows 98 DDK linker failed to create rs3d.vxd.'
}

$source = Join-Path $repoRoot 'tools\diag\rs690_3d_win32.c'
$object = Join-Path $outputDir 'rs690_3d_win32.obj'
$executable = Join-Path $outputDir 'v9xrs3d.exe'
$map = Join-Path $outputDir 'v9xrs3d-exe.map'
$linkFile = Join-Path $outputDir 'v9xrs3d-exe.lnk'
$includeDir = Join-Path $repoRoot 'include'
$build = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Executable $executable -Object $object -MapFile $map `
    -LinkFile $linkFile -LibraryNames $libraries `
    -CompileArguments @('-bt=nt', '-zq', '-wx', '-zl', '-s',
        "-i=$includeDir", "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$object", $source) `
    -LinkOptions @("option start='_V9xRs6903dProbeEntry@0'", 'option stack=65536') `
    -ToolDescription 'RS690 3D engine probe'

$vxdBytes = [IO.File]::ReadAllBytes($vxdPath)
$le = if ($vxdBytes.Length -ge 64) { [BitConverter]::ToInt32($vxdBytes, 0x3c) } else { -1 }
if ($le -lt 0 -or $le + 1 -ge $vxdBytes.Length -or
    $vxdBytes[0] -ne 0x4d -or $vxdBytes[1] -ne 0x5a -or
    $vxdBytes[$le] -ne 0x4c -or $vxdBytes[$le + 1] -ne 0x45) {
    throw 'rs3d.vxd is not an MZ/LE image.'
}
$vxdText = [Text.Encoding]::ASCII.GetString($vxdBytes)
if (-not $vxdText.Contains('RS3D_DDB')) {
    throw 'rs3d.vxd is missing its exported device descriptor.'
}

foreach ($import in @('CloseHandle', 'CreateDirectoryA', 'CreateFileA',
                       'DeviceIoControl', 'ExitProcess', 'GetDC',
                       'GetDeviceCaps', 'ReleaseDC', 'WriteFile')) {
    if ($build.DumpText -notmatch "(?m)\s$([regex]::Escape($import))\s*$") {
        throw "v9xrs3d.exe is missing import $import."
    }
}
$dllNames = [regex]::Matches($build.DumpText, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpected = @($dllNames | Where-Object {
    $_ -notin @('KERNEL32.DLL', 'GDI32.DLL', 'USER32.DLL', 'ADVAPI32.DLL')
})
if ($unexpected.Count -ne 0 -or $build.DumpText -match 'GetCommandLineW|__CHK') {
    throw 'v9xrs3d.exe contains an incompatible runtime import.'
}

Write-Output "Built RS690 3D probe VxD: $vxdPath"
Write-Output "Built RS690 3D probe: $executable"
Write-Output "Verified runtime-free imports: $($dllNames -join ', ')"
