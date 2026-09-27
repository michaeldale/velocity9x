[CmdletBinding()]
param([string]$BuildId='ati-phase1-local',[string]$DdkRoot='C:\98DDK')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$outputDir=Join-Path $repoRoot 'build\ati-mach64-phase1'
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if($BuildId -notmatch '^[A-Za-z0-9._+-]+$'){throw 'Invalid BuildId.'}
$ml=Join-Path $DdkRoot 'bin\win98\ML.EXE';$link=Join-Path $DdkRoot 'bin\LINK.EXE';$inc=Join-Path $DdkRoot 'inc\win98'
$toolchain=Get-V9xDiagToolchain -Target Win32 -LibraryNames @('kernel32.lib')
$env:WATCOM=$toolchain.WatcomRoot;$env:Path="$(Join-Path $toolchain.WatcomRoot 'binnt64');$(Join-Path $toolchain.WatcomRoot 'binnt');$env:Path";$env:INCLUDE="$(Join-Path $toolchain.WatcomRoot 'h');$(Join-Path $toolchain.WatcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir|Out-Null
$asm=Join-Path $repoRoot 'tools\diag\ati_mach64_phase1.asm';$obj=Join-Path $outputDir 'atie1.obj';$vxd=Join-Path $outputDir 'ATIEN.VXD'
& $ml '-coff' '-DBLD_COFF' '-W2' '-Zd' '-c' '-Cx' '-DMASM6' '-Sg' '-DVGA' '-DVGA31' '-DMINIVDD=1' "-I$inc" "-Fo$obj" $asm;if($LASTEXITCODE-ne 0){throw 'ATIE1 assembly failed.'}
$def=Join-Path $outputDir 'atie1.def'
@('VXD ATIEN DYNAMIC','DESCRIPTION ''ATI Mach64 Phase 1''','SEGMENTS','  _LTEXT CLASS ''LCODE'' PRELOAD NONDISCARDABLE','  _LDATA CLASS ''LCODE'' PRELOAD NONDISCARDABLE','  _TEXT CLASS ''LCODE'' PRELOAD NONDISCARDABLE','  _DATA CLASS ''LCODE'' PRELOAD NONDISCARDABLE','  CONST CLASS ''LCODE'' PRELOAD NONDISCARDABLE','  _BSS CLASS ''LCODE'' PRELOAD NONDISCARDABLE','EXPORTS','  ATIEN_DDB @1')|Set-Content -LiteralPath $def -Encoding Ascii
& $link '/VXD' '/NOD' $obj '/IGNORE:4078' '/IGNORE:4039' "/OUT:$vxd" "/DEF:$def";if($LASTEXITCODE-ne 0){throw 'ATIE1 VxD link failed.'}
$source=Join-Path $repoRoot 'tools\diag\ati_mach64_phase1_win32.c';$exe=Join-Path $outputDir 'ATIENG1.EXE';$exeObj=Join-Path $outputDir 'atie1_win32.obj';$map=Join-Path $outputDir 'atie1.map';$lnk=Join-Path $outputDir 'atie1.lnk'
$build=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $source -Executable $exe -Object $exeObj -MapFile $map -LinkFile $lnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$exeObj",$source) -LinkOptions @("option start='_V9xAtiMach64Phase1Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($build.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATIENG1.EXE is missing import $name."}}
Write-Output "Built ATI Phase 1 VxD: $vxd";Write-Output "Built ATI Phase 1 publisher: $exe"

$fillSource=Join-Path $repoRoot 'tools\diag\ati_mach64_phase2_fill_win32.c';$fillExe=Join-Path $outputDir 'ATI2D0.EXE';$fillObj=Join-Path $outputDir 'ati2d0.obj';$fillMap=Join-Path $outputDir 'ati2d0.map';$fillLnk=Join-Path $outputDir 'ati2d0.lnk'
$fillBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $fillSource -Executable $fillExe -Object $fillObj -MapFile $fillMap -LinkFile $fillLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$fillObj",$fillSource) -LinkOptions @("option start='_V9xAtiMach64Phase2Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($fillBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI2D0.EXE is missing import $name."}}
Write-Output "Built ATI Phase 2 fill publisher: $fillExe"
