[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$DdkRoot = 'C:\98DDK'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot 'build\ati-mmio-fingerprint'

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if (-not $BuildId) {
    $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'atimm-local'
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

$toolchain = Get-V9xDiagToolchain -Target Win32 -LibraryNames @('kernel32.lib', 'gdi32.lib', 'user32.lib')
$env:WATCOM = $toolchain.WatcomRoot
$env:Path = "$(Join-Path $toolchain.WatcomRoot 'binnt64');$(Join-Path $toolchain.WatcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $toolchain.WatcomRoot 'h');$(Join-Path $toolchain.WatcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$definition = Join-Path $outputDir 'atimm.def'
$vxdObject = Join-Path $outputDir 'ati_mmio_fingerprint.obj'
$vxdPath = Join-Path $outputDir 'atimm.vxd'
$vxdMap = Join-Path $outputDir 'atimm.map'
Set-Content -LiteralPath $definition -Encoding Ascii -Value @(
    'VXD ATIMM DYNAMIC',
    "DESCRIPTION 'Velocity9x ATI Mobility read-only MMIO fingerprint'",
    'SEGMENTS',
    "    _LTEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _LDATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _TEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _DATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    CONST CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "    _BSS CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    'EXPORTS',
    '    ATIMM_DDB @1'
)

$vxdSource = Join-Path $repoRoot 'tools\diag\ati_mmio_fingerprint.asm'
& $assembler '-coff' '-DBLD_COFF' '-W2' '-Zd' '-c' '-Cx' '-DMASM6' '-Sg' `
    '-DVGA' '-DVGA31' '-DMINIVDD=1' "-I$ddkInclude" `
    "-Fo$vxdObject" $vxdSource
if ($LASTEXITCODE -ne 0) {
    throw 'The Windows 98 DDK assembler failed to build ATIMM.VXD.'
}
& $vxdLinker '/VXD' '/NOD' $vxdObject '/IGNORE:4078' '/IGNORE:4039' `
    "/OUT:$vxdPath" "/MAP:$vxdMap" "/DEF:$definition"
if ($LASTEXITCODE -ne 0) {
    throw 'The Windows 98 DDK linker failed to create ATIMM.VXD.'
}

$source = Join-Path $repoRoot 'tools\diag\ati_mmio_fingerprint_win32.c'
$object = Join-Path $outputDir 'ati_mmio_fingerprint_win32.obj'
$executable = Join-Path $outputDir 'atimm.exe'
$map = Join-Path $outputDir 'atimm-exe.map'
$linkFile = Join-Path $outputDir 'atimm-exe.lnk'
$includeDir = Join-Path $repoRoot 'include'
$build = Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir `
    -Source $source -Executable $executable -Object $object -MapFile $map `
    -LinkFile $linkFile -LibraryNames @('kernel32.lib', 'gdi32.lib', 'user32.lib') `
    -CompileArguments @('-bt=nt', '-zq', '-wx', '-zl', '-s',
        "-i=$includeDir", "-dV9X_BUILD_ID=`"$BuildId`"", "-fo=$object", $source) `
    -LinkOptions @("option start='_V9xAtiMmioFingerprintEntry@0'", 'option stack=65536') `
    -ToolDescription 'ATI Mobility MMIO fingerprint publisher'

$vxdBytes = [IO.File]::ReadAllBytes($vxdPath)
$le = if ($vxdBytes.Length -ge 64) { [BitConverter]::ToInt32($vxdBytes, 0x3c) } else { -1 }
if ($le -lt 0 -or $le + 1 -ge $vxdBytes.Length -or
    $vxdBytes[0] -ne 0x4d -or $vxdBytes[1] -ne 0x5a -or
    $vxdBytes[$le] -ne 0x4c -or $vxdBytes[$le + 1] -ne 0x45) {
    throw 'ATIMM.VXD is not an MZ/LE image.'
}
$vxdText = [Text.Encoding]::ASCII.GetString($vxdBytes)
if (-not $vxdText.Contains('ATIMM_DDB')) {
    throw 'ATIMM.VXD is missing its exported device descriptor.'
}

foreach ($import in @('CloseHandle', 'CreateDirectoryA', 'CreateFileA',
                       'DeviceIoControl', 'ExitProcess', 'GetDC',
                       'GetDeviceCaps', 'ReleaseDC', 'WriteFile')) {
    if ($build.DumpText -notmatch "(?m)\s$([regex]::Escape($import))\s*$") {
        throw "ATIMM.EXE is missing import $import."
    }
}
$dllNames = [regex]::Matches($build.DumpText, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
$unexpected = @($dllNames | Where-Object { $_ -notin @('KERNEL32.DLL', 'GDI32.DLL', 'USER32.DLL') })
if ($unexpected.Count -ne 0 -or $build.DumpText -match 'GetCommandLineW|__CHK') {
    throw 'ATIMM.EXE contains an incompatible runtime import.'
}

Write-Output "Built ATI fingerprint VxD: $vxdPath"
Write-Output "Built ATI fingerprint publisher: $executable"
Write-Output "Verified runtime-free imports: $($dllNames -join ', ')"
