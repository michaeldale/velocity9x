[CmdletBinding()]
param([string]$BuildId='d3d32-probe-local')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$outputDir=Join-Path $repoRoot 'build\d3d32-probe'
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\diag-tool.ps1')
if($BuildId -notmatch '^[A-Za-z0-9._+-]+$'){throw 'Invalid BuildId.'}
New-Item -ItemType Directory -Force -Path $outputDir|Out-Null
$source=Join-Path $repoRoot 'tools\diag\d3d32_probe_win32.c';$exe=Join-Path $outputDir 'V9XD32.EXE';$obj=Join-Path $outputDir 'v9xd32.obj';$map=Join-Path $outputDir 'v9xd32.map';$lnk=Join-Path $outputDir 'v9xd32.lnk'
$build=Invoke-V9xDiagToolBuild -Target Win32 -OutputDir $outputDir -Source $source -Executable $exe -Object $obj -MapFile $map -LinkFile $lnk -LibraryNames @('kernel32.lib','user32.lib') -CompileArguments @('-bt=nt','-zq','-wx','-zl','-s',"-i=$(Join-Path $repoRoot 'include')","-dV9X_BUILD_ID=`"$BuildId`"","-fo=$obj",$source) -LinkOptions @("option start='_V9xD3d32ProbeEntry@0'",'option stack=65536')
foreach($name in @('LoadLibraryA','GetProcAddress','FlushFileBuffers','CreateWindowExA')){if($build.DumpText-notmatch "(?m)\s$([regex]::Escape($name))\s*$"){throw "V9XD32.EXE is missing import $name."}}
$dllNames=[regex]::Matches($build.DumpText,'DLL name = <([^>]+)>')|ForEach-Object{$_.Groups[1].Value.ToUpperInvariant()}|Sort-Object -Unique
if(@($dllNames|Where-Object{$_ -notin @('KERNEL32.DLL','USER32.DLL')}).Count -ne 0){throw "V9XD32.EXE imports an unexpected DLL: $($dllNames -join ', ')"}
Write-Output "Built 32 bpp Direct3D probe: $exe"
