[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$OutputDirectory
)

# Builds V9XGL.DLL, the OpenGL installable client driver (src\opengl), with
# its dispatch table generated from src\opengl\gl_entrypoints.psd1 by
# scripts\lib\gl-dispatch.ps1. Same code generation as the HAL; a per-process
# DLL, not shared. Imports KERNEL32 and USER32 only.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'lib\gl-dispatch.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'opengl-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build\opengl' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { 'C:\WATCOM' }
$compiler = Join-Path $watcomRoot 'binnt64\wcc386.exe'
$linker = Join-Path $watcomRoot 'binnt64\wlink.exe'
$dumper = Join-Path $watcomRoot 'binnt64\wdump.exe'
$env:WATCOM = $watcomRoot
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $output | Out-Null

$null = Write-V9xGlDispatchHeader -RepoRoot $repoRoot -OutputDir $output

# gl_icd.c and gl_surface.c are the two platform files; gl_state.c is the
# pure GL state, the same source the host tests compile.
$objects = @()
foreach ($name in @('gl_icd', 'gl_surface', 'gl_state', 'gl_matrix', 'gl_prim', 'gl_texture', 'gl_get', 'gl_pixels', 'gl_varray')) {
    $source = Join-Path $repoRoot "src\opengl\$name.c"
    $object = Join-Path $output "$name.obj"
    & $compiler '-bt=nt' '-bd' '-zq' '-wx' '-we' '-zl' '-s' `
        "-i=$(Join-Path $repoRoot 'include')" "-i=$output" `
        "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
    $objects += $object
}

$dll = Join-Path $output 'v9xgl.dll'
$mapFile = Join-Path $output 'v9xgl.map'
$linkFile = Join-Path $output 'v9xgl.lnk'
# The eighteen ICD exports, undecorated as OPENGL32 looks them up, each
# aliased to its stdcall-decorated symbol (argument bytes after the @).
$exports = @(
    'DrvCopyContext=_DrvCopyContext@12',
    'DrvCreateContext=_DrvCreateContext@4',
    'DrvCreateLayerContext=_DrvCreateLayerContext@8',
    'DrvDeleteContext=_DrvDeleteContext@4',
    'DrvSetContext=_DrvSetContext@12',
    'DrvReleaseContext=_DrvReleaseContext@4',
    'DrvShareLists=_DrvShareLists@8',
    'DrvDescribePixelFormat=_DrvDescribePixelFormat@16',
    'DrvSetPixelFormat=_DrvSetPixelFormat@8',
    'DrvSwapBuffers=_DrvSwapBuffers@4',
    'DrvSwapLayerBuffers=_DrvSwapLayerBuffers@8',
    'DrvDescribeLayerPlane=_DrvDescribeLayerPlane@20',
    'DrvSetLayerPaletteEntries=_DrvSetLayerPaletteEntries@20',
    'DrvGetLayerPaletteEntries=_DrvGetLayerPaletteEntries@20',
    'DrvRealizeLayerPalette=_DrvRealizeLayerPalette@12',
    'DrvGetProcAddress=_DrvGetProcAddress@4',
    'DrvValidateVersion=_DrvValidateVersion@4',
    'DrvSetCallbackProcs=_DrvSetCallbackProcs@8'
)
$lines = @('format windows nt dll', 'runtime windows=4.0', 'option quiet',
    'option nodefaultlibs', "option start='_V9xGlEntry@12'",
    "alias '__DLLstart_'='_V9xGlEntry@12'",
    "option map='$mapFile'", "option modname='V9XGL'", "name '$dll'")
$lines += $objects | ForEach-Object { "file '$_'" }
$lines += $exports | ForEach-Object { $name, $symbol = $_ -split '='; "export $name='$symbol'" }
$lines += @(
    "library '$(Join-Path $watcomRoot 'lib386\nt\kernel32.lib')'",
    "library '$(Join-Path $watcomRoot 'lib386\nt\user32.lib')'")
Set-Content -LiteralPath $linkFile -Value $lines -Encoding Ascii
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) { throw 'OpenGL ICD link failed.' }

$dump = (@(& $dumper -e $dll 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'OpenGL ICD import audit failed.' }
$imports = @([regex]::Matches($dump, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}
foreach ($export in $exports) {
    $name = ($export -split '=')[0]
    if ($dump -notmatch "\b$name\b") { throw "The OpenGL ICD does not export $name." }
}
$imageText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($dll))
if (-not $imageText.Contains($BuildId)) { throw 'The OpenGL ICD is missing the build identifier.' }
Write-Output "Built OpenGL ICD: $dll"
Write-Output "Verified imports: $($imports -join ', '); exports: $($exports.Count)"
