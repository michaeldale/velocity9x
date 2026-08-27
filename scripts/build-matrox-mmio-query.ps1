[CmdletBinding()]
param([string]$BuildId, [string]$DdkRoot = "C:\98DDK")

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\matrox-mmio-query"
. (Join-Path $PSScriptRoot "common.ps1")
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback "mga2-mmio-local" }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw "Invalid BuildId" }

$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { "C:\WATCOM" }
$assembler = Join-Path $DdkRoot "bin\win98\ML.EXE"
$vxdLinker = Join-Path $DdkRoot "bin\LINK.EXE"
$compiler = Join-Path $watcomRoot "binnt64\wcc386.exe"
$linker = Join-Path $watcomRoot "binnt64\wlink.exe"
$dumper = Join-Path $watcomRoot "binnt64\wdump.exe"
$kernel = Join-Path $watcomRoot "lib386\nt\kernel32.lib"
$ddkInclude = Join-Path $DdkRoot "inc\win98"
$required = @($assembler,$vxdLinker,$compiler,$linker,$dumper,$kernel,
              (Join-Path $ddkInclude "VMM.INC"),(Join-Path $ddkInclude "VWIN32.INC"))
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count) { throw "Missing MMIO query inputs: $($missing -join ', ')" }

$env:WATCOM = $watcomRoot
$env:Path = "$(Join-Path $watcomRoot 'binnt64');$(Join-Path $watcomRoot 'binnt');$env:Path"
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$buildInclude = Join-Path $outputDir "V9XBUILD.INC"
Set-Content -LiteralPath $buildInclude -Encoding Ascii -Value "V9xMgaQBuildId db `"velocity9x:$BuildId`", 0"
$definition = Join-Path $outputDir "v9xmgaq.def"
Set-Content -LiteralPath $definition -Encoding Ascii -Value @(
    "VXD V9XMGAQ DYNAMIC", "DESCRIPTION 'Velocity9x read-only MGA-2164W MMIO query'",
    "SEGMENTS", " _LTEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    " _LDATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    " _TEXT CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    " _DATA CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    " CONST CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    " _BSS CLASS 'LCODE' PRELOAD NONDISCARDABLE",
    "EXPORTS", " V9XMGAQ_DDB @1")
$vxdObject = Join-Path $outputDir "matrox_mmio_query.obj"
$vxd = Join-Path $outputDir "v9xmgaq.vxd"
& $assembler "-coff" "-DBLD_COFF" "-W2" "-Zd" "-c" "-Cx" "-DMASM6" "-Sg" `
    "-I$ddkInclude" "-I$outputDir" "-Fo$vxdObject" `
    (Join-Path $repoRoot "tools\diag\matrox_mmio_query.asm")
if ($LASTEXITCODE -ne 0) { throw "Failed to assemble Matrox MMIO query VxD." }
& $vxdLinker "/VXD" "/NOD" $vxdObject "/IGNORE:4078" "/IGNORE:4039" `
    "/OUT:$vxd" "/MAP:$(Join-Path $outputDir 'v9xmgaq.map')" "/DEF:$definition"
if ($LASTEXITCODE -ne 0) { throw "Failed to link Matrox MMIO query VxD." }

$object = Join-Path $outputDir "matrox_mmio_query_win32.obj"
$exe = Join-Path $outputDir "v9xmgaq.exe"
& $compiler "-bt=nt" "-zq" "-wx" "-zl" "-s" `
    "-i=$(Join-Path $repoRoot 'include')" `
    "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" `
    (Join-Path $repoRoot "tools\diag\matrox_mmio_query_win32.c")
if ($LASTEXITCODE -ne 0) { throw "Failed to compile Matrox MMIO query loader." }
$linkFile = Join-Path $outputDir "v9xmgaq.lnk"
Set-Content -LiteralPath $linkFile -Encoding Ascii -Value @(
    "format windows nt", "runtime windows=4.0", "option quiet", "option nodefaultlibs",
    "option start='_V9xMatroxMmioQueryEntry@0'", "option stack=65536",
    "name '$exe'", "file '$object'", "library '$kernel'")
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) { throw "Failed to link Matrox MMIO query loader." }

$dump = (@(& $dumper -e $exe 2>&1)) -join "`n"
foreach ($api in @("CreateFileA","DeviceIoControl","GetPrivateProfileStringA","WriteFile")) {
    if ($dump -notmatch "(?m)\s$api\s*$") { throw "MMIO query loader is missing $api" }
}
$vxdText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($vxd))
if (-not $vxdText.Contains("velocity9x:$BuildId")) { throw "MMIO query VxD lacks build identity." }
Write-Output "Built read-only Matrox MMIO query: $outputDir"
