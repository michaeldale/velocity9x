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
