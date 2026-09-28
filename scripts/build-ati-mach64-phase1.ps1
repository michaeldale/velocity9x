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

$copySource=Join-Path $repoRoot 'tools\diag\ati_mach64_phase2_copy_win32.c';$copyExe=Join-Path $outputDir 'ATI2CPY.EXE';$copyObj=Join-Path $outputDir 'ati2cpy.obj';$copyMap=Join-Path $outputDir 'ati2cpy.map';$copyLnk=Join-Path $outputDir 'ati2cpy.lnk'
$copyBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $copySource -Executable $copyExe -Object $copyObj -MapFile $copyMap -LinkFile $copyLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$copyObj",$copySource) -LinkOptions @("option start='_V9xAtiMach64Phase2CopyEntry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($copyBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI2CPY.EXE is missing import $name."}}
Write-Output "Built ATI Phase 2 copy publisher: $copyExe"

$triangleSource=Join-Path $repoRoot 'tools\diag\ati_mach64_phase3_triangle_win32.c';$triangleExe=Join-Path $outputDir 'ATI3D0.EXE';$triangleObj=Join-Path $outputDir 'ati3d0.obj';$triangleMap=Join-Path $outputDir 'ati3d0.map';$triangleLnk=Join-Path $outputDir 'ati3d0.lnk'
$triangleBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $triangleSource -Executable $triangleExe -Object $triangleObj -MapFile $triangleMap -LinkFile $triangleLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$triangleObj",$triangleSource) -LinkOptions @("option start='_V9xAtiMach64Phase3Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($triangleBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI3D0.EXE is missing import $name."}}
Write-Output "Built ATI Phase 3 triangle publisher: $triangleExe"

$gouraudExe=Join-Path $outputDir 'ATI4G0.EXE';$gouraudObj=Join-Path $outputDir 'ati4g0.obj';$gouraudMap=Join-Path $outputDir 'ati4g0.map';$gouraudLnk=Join-Path $outputDir 'ati4g0.lnk'
$gouraudBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $triangleSource -Executable $gouraudExe -Object $gouraudObj -MapFile $gouraudMap -LinkFile $gouraudLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s','-dV9X_GOURAUD=1',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$gouraudObj",$triangleSource) -LinkOptions @("option start='_V9xAtiMach64Phase3Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($gouraudBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI4G0.EXE is missing import $name."}}
Write-Output "Built ATI Phase 4 Gouraud publisher: $gouraudExe"

$ztestExe=Join-Path $outputDir 'ATI4Z0.EXE';$ztestObj=Join-Path $outputDir 'ati4z0.obj';$ztestMap=Join-Path $outputDir 'ati4z0.map';$ztestLnk=Join-Path $outputDir 'ati4z0.lnk'
$ztestBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $triangleSource -Executable $ztestExe -Object $ztestObj -MapFile $ztestMap -LinkFile $ztestLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s','-dV9X_ZTEST=1',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$ztestObj",$triangleSource) -LinkOptions @("option start='_V9xAtiMach64Phase3Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($ztestBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI4Z0.EXE is missing import $name."}}
Write-Output "Built ATI Phase 4 Z-test publisher: $ztestExe"

$zwriteExe=Join-Path $outputDir 'ATI4ZW.EXE';$zwriteObj=Join-Path $outputDir 'ati4zw.obj';$zwriteMap=Join-Path $outputDir 'ati4zw.map';$zwriteLnk=Join-Path $outputDir 'ati4zw.lnk'
$zwriteBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $triangleSource -Executable $zwriteExe -Object $zwriteObj -MapFile $zwriteMap -LinkFile $zwriteLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s','-dV9X_ZWRITE=1',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$zwriteObj",$triangleSource) -LinkOptions @("option start='_V9xAtiMach64Phase3Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($zwriteBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI4ZW.EXE is missing import $name."}}
Write-Output "Built ATI Phase 4 Z-write publisher: $zwriteExe"

$zclearExe=Join-Path $outputDir 'ATI4ZC.EXE';$zclearObj=Join-Path $outputDir 'ati4zc.obj';$zclearMap=Join-Path $outputDir 'ati4zc.map';$zclearLnk=Join-Path $outputDir 'ati4zc.lnk'
$zclearBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $triangleSource -Executable $zclearExe -Object $zclearObj -MapFile $zclearMap -LinkFile $zclearLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s','-dV9X_ZCLEAR=1',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$zclearObj",$triangleSource) -LinkOptions @("option start='_V9xAtiMach64Phase3Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($zclearBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI4ZC.EXE is missing import $name."}}
Write-Output "Built ATI Phase 4 Z-clear publisher: $zclearExe"

$textureExe=Join-Path $outputDir 'ATI4TX.EXE';$textureObj=Join-Path $outputDir 'ati4tx.obj';$textureMap=Join-Path $outputDir 'ati4tx.map';$textureLnk=Join-Path $outputDir 'ati4tx.lnk'
$textureBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $triangleSource -Executable $textureExe -Object $textureObj -MapFile $textureMap -LinkFile $textureLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s','-dV9X_TEXTURE=1',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$textureObj",$triangleSource) -LinkOptions @("option start='_V9xAtiMach64Phase3Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($textureBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI4TX.EXE is missing import $name."}}
Write-Output "Built ATI Phase 4 RGB565 texture publisher: $textureExe"

$textureStateExe=Join-Path $outputDir 'ATI4TS.EXE';$textureStateObj=Join-Path $outputDir 'ati4ts.obj';$textureStateMap=Join-Path $outputDir 'ati4ts.map';$textureStateLnk=Join-Path $outputDir 'ati4ts.lnk'
$textureStateBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $triangleSource -Executable $textureStateExe -Object $textureStateObj -MapFile $textureStateMap -LinkFile $textureStateLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s','-dV9X_TEXTURE_STATE=1',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$textureStateObj",$triangleSource) -LinkOptions @("option start='_V9xAtiMach64Phase3Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($textureStateBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI4TS.EXE is missing import $name."}}
Write-Output "Built ATI Phase 4 texture state-only publisher: $textureStateExe"

$perspectiveExe=Join-Path $outputDir 'ATI4PW.EXE';$perspectiveObj=Join-Path $outputDir 'ati4pw.obj';$perspectiveMap=Join-Path $outputDir 'ati4pw.map';$perspectiveLnk=Join-Path $outputDir 'ati4pw.lnk'
$perspectiveBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $triangleSource -Executable $perspectiveExe -Object $perspectiveObj -MapFile $perspectiveMap -LinkFile $perspectiveLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s','-dV9X_PERSPECTIVE=1',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$perspectiveObj",$triangleSource) -LinkOptions @("option start='_V9xAtiMach64Phase3Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($perspectiveBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI4PW.EXE is missing import $name."}}
Write-Output "Built ATI Phase 4 unequal-W perspective publisher: $perspectiveExe"

$wrapExe=Join-Path $outputDir 'ATI4WR.EXE';$wrapObj=Join-Path $outputDir 'ati4wr.obj';$wrapMap=Join-Path $outputDir 'ati4wr.map';$wrapLnk=Join-Path $outputDir 'ati4wr.lnk'
$wrapBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $triangleSource -Executable $wrapExe -Object $wrapObj -MapFile $wrapMap -LinkFile $wrapLnk -LibraryNames @('kernel32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s','-dV9X_WRAP=1',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$wrapObj",$triangleSource) -LinkOptions @("option start='_V9xAtiMach64Phase3Entry@0'",'option stack=65536')
foreach($name in @('CreateFileA','DeviceIoControl','WriteFile','ExitProcess')){if($wrapBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "ATI4WR.EXE is missing import $name."}}
Write-Output "Built ATI Phase 4 nearest wrap S/T publisher: $wrapExe"
