[CmdletBinding()]
param([string]$BuildId='ddraw-fill-stage-local')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$outputDir=Join-Path $repoRoot 'build\ddraw-fill-stage'
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if($BuildId -notmatch '^[A-Za-z0-9._+-]+$'){throw 'Invalid BuildId.'}
New-Item -ItemType Directory -Force -Path $outputDir|Out-Null
$source=Join-Path $repoRoot 'tools\diag\ddraw_fill_stage_win32.c';$exe=Join-Path $outputDir 'V9XDDF.EXE';$obj=Join-Path $outputDir 'v9xddf.obj';$map=Join-Path $outputDir 'v9xddf.map';$lnk=Join-Path $outputDir 'v9xddf.lnk'
$build=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $source -Executable $exe -Object $obj -MapFile $map -LinkFile $lnk -LibraryNames @('kernel32.lib','user32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$obj",$source) -LinkOptions @("option start='_V9xDdFillStageEntry@0'",'option stack=65536')
foreach($name in @('LoadLibraryA','GetProcAddress','FlushFileBuffers','GetDesktopWindow')){if($build.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "V9XDDF.EXE is missing import $name."}}
Write-Output "Built staged DirectDraw fill probe: $exe"

$txmSource=Join-Path $repoRoot 'tools\diag\ddraw_texture_stage_win32.c';$txmExe=Join-Path $outputDir 'V9XTXM.EXE';$txmObj=Join-Path $outputDir 'v9xtxm.obj';$txmMap=Join-Path $outputDir 'v9xtxm.map';$txmLnk=Join-Path $outputDir 'v9xtxm.lnk'
$txmBuild=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $txmSource -Executable $txmExe -Object $txmObj -MapFile $txmMap -LinkFile $txmLnk -LibraryNames @('kernel32.lib','user32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s',"-dV9X_BUILD_ID=`"$BuildId`"","-fo=$txmObj",$txmSource) -LinkOptions @("option start='_V9xTextureStageEntry@0'",'option stack=65536')
foreach($name in @('LoadLibraryA','GetProcAddress','FlushFileBuffers','GetDesktopWindow')){if($txmBuild.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "V9XTXM.EXE is missing import $name."}}
Write-Output "Built staged DirectDraw texture-traffic probe: $txmExe"
