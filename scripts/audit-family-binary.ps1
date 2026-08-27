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
    [string]$DriverName = "v9xdisp",
    # DGROUP budget, in bytes. A Win16 automatic data segment cannot exceed
    # 64 KiB, and DGROUP has to hold the driver's static data, its local heap
    # and its stack at run time - so the gate is set at half the hard limit
    # rather than at it. The dynamic-VBE runtime mode table is the first thing
    # in this driver's history to want kilobytes of DGROUP rather than bytes
    # (64 rows: 896 bytes of V9X_HW16_MODE, 768 of colour masks, 64 of
    # publication flags = 1728), which is why the number is now asserted at
    # every build instead of being watched by hand.
    [int]$DgroupBudgetBytes = 32768
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

# DGROUP occupancy, from the linker's own group table. Reported on every build
# so growth is visible in the log before it is a failure, and asserted so a
# table that outgrows the segment fails here rather than at boot on hardware,
# where an over-full automatic data segment is not a diagnosable symptom.
if ($mapText -notmatch '(?m)^DGROUP\s+[0-9A-Fa-f]{4}:[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)\s*$') {
    throw "The $($target.Id) map has no DGROUP group entry to size."
}
$dgroupBytes = [Convert]::ToInt32($Matches[1], 16)
# The local heap is declared in the link file, not the map, and it comes out of
# the same 64 KiB. Read it back rather than assuming the current 1024.
$linkFile = Join-Path $OutputDir "$DriverName.lnk"
$heapBytes = 0
if (Test-Path -LiteralPath $linkFile) {
    foreach ($line in (Get-Content -LiteralPath $linkFile)) {
        if ($line -match '^\s*option\s+heapsize\s*=\s*([0-9]+)\s*$') {
            $heapBytes = [int]$Matches[1]
        }
    }
}
$dgroupTotal = $dgroupBytes + $heapBytes
if ($dgroupTotal -gt $DgroupBudgetBytes) {
    throw ("The $($target.Id) driver's DGROUP is $dgroupBytes bytes plus a " +
           "$heapBytes-byte local heap = $dgroupTotal, over the " +
           "$DgroupBudgetBytes-byte budget. The Win16 automatic data segment " +
           "hard limit is 65536 including the stack; move new static data out " +
           "of DGROUP rather than raising this.")
}
$dgroupSummary = ("DGROUP $dgroupBytes + heap $heapBytes = $dgroupTotal of " +
                  "$DgroupBudgetBytes budget")
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
    "DIB_MoveCursorExt", "DIB_CheckCursorExt",
    # GDI acceleration. Script-level rather than manifest-driven because
    # src\display16\gdi_accel.c links into every family: three of the four have
    # no 2D engine and take its decline path on every blit for ever, so these
    # symbols are as load-bearing there as on the S3.
    #
    # BITBLT is ordinal 1 and is a C function now; V9XDIBBITBLTCALL is the only
    # route back to the DIB Engine, so a build that lost it would have replaced
    # the passthrough with nothing.
    "BITBLT", "V9XDIBBITBLTCALL",
    "V9XGDIBEGINACCESSSLOW", "_v9x_gdi_engine_dirty", "_v9x_gdi_poisoned",
    "V9XENGINESELECTOR", "V9XENGINEREAD", "V9XENGINEWRITE"
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
    'mov\s+ax,seg RESETHIRESMODE', 'int\s+2fH'
)
foreach ($instruction in $commonRuntimeInstructions) {
    if ($runtimeDisassembly -notmatch $instruction) {
        throw "The Win16 runtime is missing audited VDD handoff instruction $instruction."
    }
}
# The VBE mode set moved out of runtime.asm into the shared vbe16 service, so
# this one is audited across the image rather than against the assembly.
# The compiler spells a DGROUP reference without the group prefix the
# assembler emits, so these match the bare symbol.
$commonImageInstructions = @(
    'mov\s+\w+,word ptr _v9x_active_vbe_mode',
    'mov\s+\w+,word ptr _v9x_vbe_mode_flags',
    'mov\s+ax,4f02H',
    'int\s+10H'
)
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
    # Historical trap: this message says "BitBlt thunk" in older trees, but the
    # pattern audits DibBlt - ordinal 19, not ordinal 1.
    throw "The DIB DibBlt thunk is not forwarding the selected palette mode."
}
# Ordinal 1 must no longer be an assembly forward. If the dispatcher were ever
# reverted by re-adding V9X_FORWARD BitBlt to dib_thunks.asm, the link would
# pick a thunk over the C function and every gate below would silently stop
# running - a passing build that had quietly lost the feature.
if ($thunkDisassembly -match '(?m)^BitBlt:') {
    throw ("dib_thunks.asm still forwards BitBlt. Ordinal 1 belongs to the C " +
           "dispatcher in src\display16\gdi_accel.c.")
}

# ---------------------------------------------------------------------------
# GDI acceleration: the decline path must still reach the DIB Engine.
#
# This is the one new disassembly audit worth its lines. Every acceptance gate
# in the dispatcher ends in the same decline branch, and on three of the four
# families that branch is the only branch, on every blit, for ever. A build in
# which it stopped calling through to DIB_BitBlt would not draw at all - and it
# would still link, still export ordinal 1, and still pass every other check
# here.
#
# Two objects, because the route is two hops: gdi_accel.c cannot name
# DIB_BitBlt directly (its PASCAL exports are uppercased, DIBENG.LIB's symbol
# is not), so it calls the typed V9XDIBBITBLTCALL wrapper in runtime.asm, which
# tail-jumps to DIB_BitBlt.
# ---------------------------------------------------------------------------

$gdiAccelObject = Join-Path $OutputDir "gdi_accel.obj"
if (-not (Test-Path -LiteralPath $gdiAccelObject)) {
    throw ("Audit input is missing: $gdiAccelObject. Every family links " +
           "src\display16\gdi_accel.c; a family manifest that dropped it has " +
           "no ordinal 1.")
}
$gdiDisassembly = (& $disassembler "-a" $gdiAccelObject 2>&1) -join "`n"
if ($gdiDisassembly -notmatch '(?m)^\s*PUBLIC\s+BITBLT\s*$') {
    throw "gdi_accel.obj does not export BITBLT, so ordinal 1 has no owner."
}
if ($gdiDisassembly -notmatch
    '(?sm)^BITBLT:.*?call\s+far ptr V9XDIBBITBLTCALL') {
    throw ("The GDI BitBlt dispatcher's decline branch does not reach " +
           "V9XDIBBITBLTCALL, so a declined blit would draw nothing.")
}
if ($runtimeDisassembly -notmatch
    '(?s)V9XDIBBITBLTCALL:\s*jmp\s+far ptr DIB_BitBlt') {
    throw "V9XDIBBITBLTCALL does not forward to the DIB Engine's BitBlt."
}
# Both deBeginAccess entry points carry the dirty check. Reasoning per caller
# about which one can never race pending engine work is a worse trade than a
# shared stub, so the audit asserts both rather than one.
foreach ($accessEntry in @("V9XDIBBEGINACCESS", "V9XDIBBEGINACCESSRECT")) {
    if ($runtimeDisassembly -notmatch
        ("(?s)$accessEntry" + ':\s*mov\s+ax,DGROUP.*?cmp\s+word ptr ' +
         'es:_v9x_gdi_engine_dirty,0.*?jmp\s+far ptr DIB_BeginAccess.*?' +
         'call\s+far ptr V9XGDIBEGINACCESSSLOW')) {
        throw ("$accessEntry is missing the GDI engine dirty check, so a CPU " +
               "framebuffer access could overtake pending engine work.")
    }
}
# The ViRGE command and status registers are 32 bits wide and this driver is
# built without a -3, so a C `volatile DWORD FAR *` access compiles to two
# 16-bit halves. On CMD_SET that is wrong rather than slow: the low half starts
# the blit. Assert the single-instruction accesses the assembly exists to
# provide.
foreach ($engineAccess in @('mov\s+eax,dword ptr es:\[bx\]',
                            'mov\s+dword ptr es:\[bx\],eax')) {
    if ($runtimeDisassembly -notmatch $engineAccess) {
        throw ("The ViRGE MMIO accessors are missing 32-bit-wide access " +
               "$engineAccess; a split access would trigger the engine on a " +
               "half-written command.")
    }
}

# ---------------------------------------------------------------------------
# Layer 1: cross-family contamination.
# ---------------------------------------------------------------------------

# Scan every object in the image, not just the assembled runtime. Chip code
# now lives in per-family C modules, and a signature check that only looked at
# runtime.obj would silently pass once the code it audits moved out of it.
$imageDisassembly = (@(Get-ChildItem -LiteralPath $OutputDir -Filter "*.obj" -File |
    Sort-Object Name | ForEach-Object {
        (& $disassembler "-a" $_.FullName 2>&1) -join "`n"
    }) -join "`n")

foreach ($instruction in $commonImageInstructions) {
    if ($imageDisassembly -notmatch $instruction) {
        throw "The $($target.Id) image is missing audited mode-set instruction $instruction."
    }
}

$requiredPatterns = @(Get-V9xFamilyRequiredPatterns -Family $target)
$forbiddenPatterns = @(Get-V9xFamilyForbiddenPatterns -Family $target `
    -AllFamilies $families)
foreach ($pattern in $requiredPatterns) {
    if ($imageDisassembly -notmatch $pattern) {
        throw "The $($target.Id) image is missing audited instruction $pattern."
    }
}
foreach ($pattern in $forbiddenPatterns) {
    if ($imageDisassembly -match $pattern) {
        throw ("The $($target.Id) image contains foreign-family instruction " +
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
              "$($foreignSymbols.Count) forbidden map symbol(s); $dgroupSummary.")
