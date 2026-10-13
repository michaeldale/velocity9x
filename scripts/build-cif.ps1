# Build ATI3DCIF.DLL, the 3DCIF census (docs\plans\ati-3dcif-wrapper.md,
# Phase 1): every export of ATI's ATI3DCIF.DLL, logging to
# C:\V9XDIAG\V9XCIF.LOG and drawing nothing. Not packaged: it is copied
# into a demo's or title's own folder for a census run, where Windows finds
# it before the system directory's copy.
#
# Each manifest entry is exported twice, as ATI's DLL does: the __stdcall
# function _ATI3DCIF_<Name>@<Bytes>, and ATI3DCIF_<Name>_lib, the variable
# holding its address. The audit checks both lists exactly, and that the
# _lib names land in a data section, not code: a caller reads a pointer
# there.
[CmdletBinding()]
param(
    [string]$BuildId,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $BuildId) { $BuildId = Get-V9xBuildId -RepoRoot $repoRoot -Fallback 'cif-local' }
if ($BuildId -notmatch '^[A-Za-z0-9._+-]+$') { throw 'Invalid BuildId.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build\cif' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$watcomRoot = if ($env:WATCOM) { $env:WATCOM } else { 'C:\WATCOM' }
$compiler = Join-Path $watcomRoot 'binnt64\wcc386.exe'
$linker = Join-Path $watcomRoot 'binnt64\wlink.exe'
$dumper = Join-Path $watcomRoot 'binnt64\wdump.exe'
$env:WATCOM = $watcomRoot
$env:INCLUDE = "$(Join-Path $watcomRoot 'h');$(Join-Path $watcomRoot 'h\nt')"
New-Item -ItemType Directory -Force -Path $output | Out-Null

$manifest = Import-PowerShellDataFile -LiteralPath (Join-Path $repoRoot 'src\cif\cif_entrypoints.psd1')
$entries = @($manifest.Entries)
if ($entries.Count -ne 19) { throw "cif_entrypoints.psd1 has $($entries.Count) entries; ATI's DLL has 19." }
foreach ($e in $entries) {
    if ([string]$e.Name -notmatch '^[A-Za-z]+$') { throw "cif_entrypoints.psd1: bad name '$($e.Name)'." }
    if (([int]$e.Bytes % 4) -ne 0 -or [int]$e.Bytes -gt 16) { throw "cif_entrypoints.psd1: $($e.Name) has Bytes $($e.Bytes)." }
}
$functions = @($entries | ForEach-Object { "_ATI3DCIF_$($_.Name)@$([int]$_.Bytes)" })
$pointers = @($entries | ForEach-Object { "ATI3DCIF_$($_.Name)_lib" })

$source = Join-Path $repoRoot 'src\cif\cif_census.c'
$object = Join-Path $output 'cif_census.obj'
& $compiler '-bt=nt' '-bd' '-zq' '-wx' '-we' '-zl' '-s' '-ox' `
    "-i=$(Join-Path $repoRoot 'include')" `
    "-dV9X_BUILD_ID=`"$BuildId`"" "-fo=$object" $source
if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }

$dll = Join-Path $output 'ATI3DCIF.DLL'
$mapFile = Join-Path $output 'ati3dcif.map'
$linkFile = Join-Path $output 'ati3dcif.lnk'
$lines = @('format windows nt dll', 'runtime windows=4.0', 'option quiet',
    'option nodefaultlibs', "option start='_V9xCifEntry@12'",
    "alias '__DLLstart_'='_V9xCifEntry@12'",
    "option map='$mapFile'", "option modname='ATI3DCIF'", "name '$dll'",
    "file '$object'")
$lines += ($functions + $pointers) | ForEach-Object { "export '$_'" }
$lines += @(
    "library '$(Join-Path $watcomRoot 'lib386\nt\kernel32.lib')'",
    "library '$(Join-Path $watcomRoot 'lib386\nt\user32.lib')'")
Set-Content -LiteralPath $linkFile -Value $lines -Encoding Ascii
& $linker "@$linkFile"
if ($LASTEXITCODE -ne 0) { throw '3DCIF DLL link failed.' }
Add-V9xVersionResource -RepoRoot $repoRoot -WatcomRoot $watcomRoot -Image $dll `
    -BuildId $BuildId -FileDescription 'Velocity9x ATI3DCIF census' -Kind dll

$dump = (@(& $dumper -e $dll 2>&1)) -join "`n"
if ($LASTEXITCODE -ne 0) { throw '3DCIF DLL import audit failed.' }
$imports = @([regex]::Matches($dump, 'DLL name = <([^>]+)>') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
if (@($imports | Where-Object { $_ -notin @('KERNEL32.DLL', 'USER32.DLL') }).Count) {
    throw "Unexpected imports: $($imports -join ', ')"
}

# The export directory read straight from the image: name and RVA, so the
# audit can tell a pointer in .data from a function in .text.
$bytes = [IO.File]::ReadAllBytes($dll)
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
$sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
$optionalSize = [BitConverter]::ToUInt16($bytes, $pe + 20)
$exportRva = [BitConverter]::ToUInt32($bytes, $pe + 24 + 96)
$sections = @(for ($i = 0; $i -lt $sectionCount; $i++) {
    $s = $pe + 24 + $optionalSize + 40 * $i
    [pscustomobject]@{
        Flags = [BitConverter]::ToUInt32($bytes, $s + 36)
        Va = [BitConverter]::ToUInt32($bytes, $s + 12)
        Size = [Math]::Max([BitConverter]::ToUInt32($bytes, $s + 8), [BitConverter]::ToUInt32($bytes, $s + 16))
        Raw = [BitConverter]::ToUInt32($bytes, $s + 20)
    }
})
function Get-SectionOf([uint32]$Rva) {
    foreach ($s in $sections) { if ($Rva -ge $s.Va -and $Rva -lt $s.Va + $s.Size) { return $s } }
    throw "RVA $('{0:X8}' -f $Rva) is in no section."
}
function Get-FileOffset([uint32]$Rva) { $s = Get-SectionOf $Rva; return [int]($Rva - $s.Va + $s.Raw) }
$e = Get-FileOffset $exportRva
$nameCount = [BitConverter]::ToUInt32($bytes, $e + 24)
$functionTable = Get-FileOffset ([BitConverter]::ToUInt32($bytes, $e + 28))
$nameTable = Get-FileOffset ([BitConverter]::ToUInt32($bytes, $e + 32))
$ordinalTable = Get-FileOffset ([BitConverter]::ToUInt32($bytes, $e + 36))
$exported = @{}
for ($i = 0; $i -lt $nameCount; $i++) {
    $nameAt = Get-FileOffset ([BitConverter]::ToUInt32($bytes, $nameTable + 4 * $i))
    $end = [Array]::IndexOf($bytes, [byte]0, $nameAt)
    $name = [Text.Encoding]::ASCII.GetString($bytes, $nameAt, $end - $nameAt)
    $ordinal = [BitConverter]::ToUInt16($bytes, $ordinalTable + 2 * $i)
    $exported[$name] = [BitConverter]::ToUInt32($bytes, $functionTable + 4 * $ordinal)
}
$expected = $functions + $pointers
$missing = @($expected | Where-Object { -not $exported.ContainsKey($_) })
if ($missing.Count) { throw "The 3DCIF DLL does not export: $($missing -join ', ')" }
$extra = @($exported.Keys | Where-Object { $_ -notin $expected })
if ($extra.Count) { throw "The 3DCIF DLL exports names outside the manifest: $($extra -join ', ')" }
$IMAGE_SCN_CNT_CODE = 0x20
foreach ($name in $functions) {
    if (((Get-SectionOf $exported[$name]).Flags -band $IMAGE_SCN_CNT_CODE) -eq 0) { throw "$name is not in a code section." }
}
foreach ($name in $pointers) {
    if (((Get-SectionOf $exported[$name]).Flags -band $IMAGE_SCN_CNT_CODE) -ne 0) { throw "$name is in a code section; it must be a pointer variable." }
}
$imageText = [System.Text.Encoding]::ASCII.GetString($bytes)
if (-not $imageText.Contains($BuildId)) { throw 'The 3DCIF DLL is missing the build identifier.' }
Write-Output "Built 3DCIF census DLL: $dll"
Write-Output "Verified imports: $($imports -join ', '); exports: $($functions.Count) functions, $($pointers.Count) _lib pointers in data"
