# Post-link audit for a Velocity9x family driver image.
#
# Replaces the per-chip wdis instruction checks that used to live inside
# build-win16-ddi-skeleton.ps1. Those checks assumed one chip per binary and
# break as soon as a family image legitimately contains several chips' code.
#
# Three manifest-driven layers, plus the chip-agnostic checks that stay as
# script logic because they are the same for every family:
#
#   1. Cross-family contamination - the image must match all of its own chips'
#      signatures and none of any other family's. The forbidden list is derived
#      from the other manifests, so adding a family strengthens every existing
#      family's audit with no edit here.
#   2. Per-chip-object audits - once per-chip code lives in its own object, a
#      chip's object must carry that chip's signatures and none of its
#      siblings'. Manifest-driven through Chips[].Objects; inert until the
#      per-chip modules land.
#   3. Link-map symbol audits - required per-chip symbols, the family dispatch
#      table symbol, and no other family's backend symbols.
#
# See docs\plans\multi-chip-restructure.md.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Family,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [Parameter(Mandatory = $true)][string]$BuildId,
    [switch]$BootTrace,
    [string]$DriverName = "v9xdisp"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "lib\family.ps1")

$families = @(Get-V9xFamilies -RepoRoot $repoRoot)
$target = @($families | Where-Object { $_.Id -eq $Family })
if ($target.Count -ne 1) {
    throw "Unknown family '$Family'."
}
$target = $target[0]

$watcomRoot = $env:WATCOM
if (-not $watcomRoot -and (Test-Path -LiteralPath "C:\WATCOM")) {
    $watcomRoot = "C:\WATCOM"
}
if (-not $watcomRoot) {
    throw "Open Watcom was not found. Set WATCOM or install it at C:\WATCOM."
}
$dumper = Join-Path $watcomRoot "binnt64\wdump.exe"
$disassembler = Join-Path $watcomRoot "binnt64\wdis.exe"

$driverPath = Join-Path $OutputDir "$DriverName.drv"
$mapPath = Join-Path $OutputDir "$DriverName.map"
$runtimeObject = Join-Path $OutputDir "runtime.obj"
$thunkObject = Join-Path $OutputDir "dib_thunks.obj"
foreach ($input in @($driverPath, $mapPath, $runtimeObject, $thunkObject)) {
    if (-not (Test-Path -LiteralPath $input)) {
        throw "Audit input is missing: $input"
    }
}

# ---------------------------------------------------------------------------
# Chip-agnostic image checks.
# ---------------------------------------------------------------------------

$bytes = [System.IO.File]::ReadAllBytes($driverPath)
$newHeaderOffset = if ($bytes.Length -ge 64) {
    [System.BitConverter]::ToInt32($bytes, 0x3c)
} else { -1 }
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a -or
    $newHeaderOffset -lt 0 -or $newHeaderOffset + 1 -ge $bytes.Length -or
    $bytes[$newHeaderOffset] -ne 0x4e -or
    $bytes[$newHeaderOffset + 1] -ne 0x45) {
    throw "The Win16 DDI output is not an MZ/NE image."
}
if (-not [System.Text.Encoding]::ASCII.GetString($bytes).Contains($BuildId)) {
    throw "The Win16 DDI output does not contain the build identifier."
}

$exports = (& $dumper "-x" $driverPath 2>&1) -join "`n"
$requiredExports = @("Enable", "Disable", "BitBlt", "ReEnable",
                     "Inquire", "SetCursor", "MoveCursor", "CheckCursor",
                     "ValidateMode")
foreach ($requiredExport in $requiredExports) {
    if ($exports -cnotmatch "(?m)^\s*$requiredExport\s+@") {
        throw "The Win16 DDI output is missing export $requiredExport."
    }
}

$image = (& $dumper "-e" $driverPath 2>&1) -join "`n"
if ($image -notmatch "DIBENG") {
    throw "The Win16 DDI output does not import the DIB Engine."
}
if ($image -notmatch "CODE\|FIXED\|SHARE\|PRELOAD") {
    throw "The Win16 DDI code segment is not fixed, shared, and preloaded."
}
if ($image -notmatch "DATA\|FIXED\|(SHARE\|)?PRELOAD\|READWRITE") {
    throw "The Win16 DDI data segment is not fixed and preloaded."
}
if ($image -notmatch "(?m)^DISPLAY\s+unknown ordinal 0000$") {
    throw "The Win16 DDI internal module name is not DISPLAY."
}

$mapText = Get-Content -LiteralPath $mapPath -Raw
if ($mapText -notmatch "(?m)^.*DriverInit.*$") {
    throw "The Win16 DDI map is missing the DriverInit entry point."
}
foreach ($forbiddenStartup in @('__DLLstart_', 'WINMAIN', 'DEFAULTWINMAIN',
                                 'main_')) {
    if ($mapText -match [regex]::Escape($forbiddenStartup)) {
        throw "The Win16 DDI pulled forbidden C startup symbol $forbiddenStartup."
    }
}
if ($BootTrace) {
    if ($mapText -notmatch "WRITEPRIVATEPROFILESTRING\s+KERNEL") {
        throw "The traced Win16 DDI does not import WritePrivateProfileString."
    }
    $imageText = [System.Text.Encoding]::ASCII.GetString($bytes)
    foreach ($marker in @("libmain", "trace-write-fail stage=libmain")) {
        if (-not $imageText.Contains($marker)) {
            throw "The traced Win16 DDI is missing marker $marker."
        }
    }
}

$requiredRuntimeSymbols = @(
    "V9XHARDWAREPRESENT", "V9XHARDWAREENABLE", "V9XHARDWAREDISABLE",
    "V9XHARDWARESTAGE",
    "V9XVDDGETDISPLAYCONFIG", "V9XVDDPREMODE", "V9XVDDREGISTER", "V9XVDDUNREGISTER",
    "V9XVDDPOSTMODE",
    "V9XCREATEDIBPDEVICECALL", "V9XDIBSETPALETTETRANSLATECALL",
    "DIB_EnumObjExt", "DIB_RealizeObjectExt",
    "DIB_DibBltExt", "DIB_GetPaletteExt", "DIB_SetCursorExt",
    "DIB_MoveCursorExt", "DIB_CheckCursorExt"
)
foreach ($symbol in $requiredRuntimeSymbols) {
    if ($mapText -notmatch "(?m)^.*$([regex]::Escape($symbol)).*$") {
        throw "The Win16 DDI map is missing runtime symbol $symbol."
    }
}

$runtimeDisassembly = (& $disassembler "-a" $runtimeObject 2>&1) -join "`n"
$commonRuntimeInstructions = @(
    'mov\s+eax,80H', 'mov\s+eax,81H', 'mov\s+eax,82H',
    'mov\s+eax,85H', 'mov\s+eax,86H', 'mov\s+eax,87H',
    'push\s+esi', 'push\s+edi',
    'movzx\s+edi,word ptr 6\[bp\]',
    'xor\s+edx,edx',
    'mov\s+ecx,dword ptr DGROUP:_v9x_active_visible_bytes',
    'mov\s+bx,word ptr DGROUP:_v9x_active_vbe_mode',
    'mov\s+ax,seg RESETHIRESMODE', 'int\s+2fH'
)
foreach ($instruction in $commonRuntimeInstructions) {
    if ($runtimeDisassembly -notmatch $instruction) {
        throw "The Win16 runtime is missing audited VDD handoff instruction $instruction."
    }
}
foreach ($instruction in @('pop\s+dword ptr .*',
                            'call\s+far ptr CreateDIBPDevice',
                            'push\s+dword ptr .*', 'mov\s+dx,ax',
                            'shr\s+eax,10H', 'xchg\s+ax,dx')) {
    if ($runtimeDisassembly -notmatch $instruction) {
        throw "The CreateDIBPDevice thunk is missing ABI fixup $instruction."
    }
}

$thunkDisassembly = (& $disassembler "-a" $thunkObject 2>&1) -join "`n"
if ($thunkDisassembly -notmatch
    '(?s)CheckCursor:.*?cmp\s+dword ptr es:_v9x_driver_pdevice,0.*?jmp\s+far ptr DIB_CheckCursorExt.*?retf') {
    throw "The DIB CheckCursor thunk is missing its disabled-state guard."
}
foreach ($cursorThunk in @(
    @("SetCursor", "DIB_SetCursorExt"),
    @("MoveCursor", "DIB_MoveCursorExt")
)) {
    if ($thunkDisassembly -notmatch
        ("(?s)$($cursorThunk[0]):.*?cmp\s+dword ptr " +
         "es:_v9x_driver_pdevice,0.*?jmp\s+far ptr " +
         "$($cursorThunk[1]).*?retf\s+(?:4|0004H)")) {
        throw "The DIB $($cursorThunk[0]) thunk is missing its disabled-state guard."
    }
}
if ($thunkDisassembly -notmatch
    '(?s)DibBlt:.*?push\s+word ptr es:_v9x_palettized.*?jmp\s+far ptr DIB_DibBltExt') {
    throw "The DIB BitBlt thunk is not forwarding the selected palette mode."
}

# ---------------------------------------------------------------------------
# Layer 1: cross-family contamination.
# ---------------------------------------------------------------------------

$requiredPatterns = @(Get-V9xFamilyRequiredPatterns -Family $target)
$forbiddenPatterns = @(Get-V9xFamilyForbiddenPatterns -Family $target `
    -AllFamilies $families)
foreach ($pattern in $requiredPatterns) {
    if ($runtimeDisassembly -notmatch $pattern) {
        throw "The $($target.Id) runtime is missing audited instruction $pattern."
    }
}
foreach ($pattern in $forbiddenPatterns) {
    if ($runtimeDisassembly -match $pattern) {
        throw ("The $($target.Id) runtime contains foreign-family instruction " +
               "$pattern.")
    }
}

# ---------------------------------------------------------------------------
# Layer 2: per-chip objects.
# ---------------------------------------------------------------------------

$perChipObjects = 0
foreach ($chip in @($target.Chips)) {
    foreach ($objectName in @($chip.Objects | Where-Object { $_ })) {
        $objectPath = Join-Path $OutputDir "$objectName.obj"
        if (-not (Test-Path -LiteralPath $objectPath)) {
            throw ("Family $($target.Id) chip $($chip.Id) declares object " +
                   "$objectName, which the build did not produce.")
        }
        $objectDisassembly = (& $disassembler "-a" $objectPath 2>&1) -join "`n"
        foreach ($pattern in @($chip.Audit.Required)) {
            if ($objectDisassembly -notmatch $pattern) {
                throw ("Object $objectName is missing $($chip.Id) signature " +
                       "$pattern.")
            }
        }
        $chipForbidden = @(Get-V9xChipForbiddenPatterns -Family $target `
            -Chip $chip -AllFamilies $families)
        foreach ($pattern in $chipForbidden) {
            if ($objectDisassembly -match $pattern) {
                throw ("Object $objectName contains foreign chip signature " +
                       "$pattern.")
            }
        }
        ++$perChipObjects
    }
}

# ---------------------------------------------------------------------------
# Layer 3: link-map symbols.
# ---------------------------------------------------------------------------

$requiredSymbols = @($target.Audit.RequiredMapSymbols)
if ($target.Audit.DispatchSymbol) {
    $requiredSymbols += $target.Audit.DispatchSymbol
}
foreach ($chip in @($target.Chips)) {
    $requiredSymbols += @($chip.MapSymbols)
}
foreach ($symbol in @($requiredSymbols | Where-Object { $_ } | Sort-Object -Unique)) {
    if ($mapText -notmatch "(?m)^.*$([regex]::Escape($symbol)).*$") {
        throw "The $($target.Id) map is missing required symbol $symbol."
    }
}

$ownSymbols = @($requiredSymbols | Where-Object { $_ })
$foreignSymbols = @(@($families) |
    Where-Object { $_.Id -ne $target.Id } |
    ForEach-Object {
        @($_.Audit.BackendSymbols) + @($_.Audit.DispatchSymbol) +
        @(@($_.Chips) | ForEach-Object { @($_.MapSymbols) })
    } | Where-Object { $_ -and $_ -notin $ownSymbols } | Sort-Object -Unique)
foreach ($symbol in $foreignSymbols) {
    if ($mapText -match "(?m)^.*$([regex]::Escape($symbol)).*$") {
        throw "The $($target.Id) map contains foreign-family symbol $symbol."
    }
}

Write-Output ("Audited $($target.Id) image: $($requiredPatterns.Count) required " +
              "and $($forbiddenPatterns.Count) forbidden signatures, " +
              "$perChipObjects per-chip object(s), " +
              "$(@($requiredSymbols | Where-Object { $_ }).Count) required and " +
              "$($foreignSymbols.Count) forbidden map symbol(s).")
