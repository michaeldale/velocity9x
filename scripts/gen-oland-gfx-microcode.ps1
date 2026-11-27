[CmdletBinding()]
param(
    [string]$FirmwareDirectory,
    [string]$SiFirmwareDirectory
)

# Generates src\chipsets\ati\si\si_gfx_microcode.c, the AMD Southern
# Islands (GCN 1) graphics engine's microcode as host-order dwords - the
# RLC's and the CP's three microengines' (PFP, CE, ME) - from
# linux-firmware's amdgpu/{oland,verde,pitcairn,tahiti}_{rlc,pfp,ce,me}.bin
# and LICENSE.amdgpu. Each file must match the SHA-256 recorded here. The
# Oland's images ran on W9QI0DAD (docs\plans\ati-oland-gfx6.md); Cape
# Verde's, Pitcairn's and Tahiti's have run on no card
# (docs\decisions\2026-11-28-si-cape-verde-pitcairn-tahiti-from-surveys.md).
# A copy of gen-mullins-gfx-microcode.ps1.
#
# An image is emitted once. Where another chip's file is byte for byte one
# already emitted, it is not emitted again and the chip uses that array;
# the identities are asserted here, so a firmware update that breaks one
# fails the generator rather than silently loading the wrong image. As
# downloaded on 2026-11-28: every CE is the Oland's; Pitcairn's and
# Tahiti's ME are the Oland's; Tahiti's PFP is Pitcairn's. Each RLC is its
# own, and so are Cape Verde's PFP and ME.
#
# An amdgpu image is a little-endian common_firmware_header (Linux
# amdgpu_ucode.h) and a payload; the array is the payload from
# ucode_array_offset_bytes, ucode_size_bytes long, which Linux
# gfx_v6_0_rlc_resume and gfx_v6_0_cp_gfx_load_microcode write to each
# engine's data port. Unlike gfx_v7_0, gfx_v6_0 writes 0 (not the
# ucode_version) to the address ports after the load, and the RLC load
# writes RLC_UCODE_ADDR before every dword; the versions are emitted for
# the diagnostics only. Every header is version 1.0, and every chip's
# images are the Oland's sizes.
#
# The radeon/*.bin files are not used: the amdgpu copies are the ones the
# current Linux GFX6 code loads.

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $FirmwareDirectory) {
    $FirmwareDirectory = Join-Path $repoRoot 'build\external\oland\firmware'
}
if (-not $SiFirmwareDirectory) {
    $SiFirmwareDirectory = Join-Path $repoRoot 'build\external\si\firmware'
}

$expected = [ordered]@{
    'oland_rlc.bin'    = '012b8e872a0d6dbd718d5fcba462ba088ac39289dd6b8b66cce886698ff9314b'
    'oland_pfp.bin'    = '1beadbc0b81464c8a85b9e2d981fb83f03ec11c365034332a978b94a6785148f'
    'oland_ce.bin'     = '04027c3a7c875aaf581ca438ae5ff93ec90002e0e057dc107e7b47048331ee3c'
    'oland_me.bin'     = '87617358bb876187786c422c3456fef1d072e6532382ecace0847259045131cb'
    'verde_rlc.bin'    = '50c69aef44d0047e49b124937edd001d5828b3fa3e00aceebc78af48a5760fa0'
    'verde_pfp.bin'    = 'a76ae8ae9e7c32118247a23fdc53086f1c91026dca87b732d39893b33873158e'
    'verde_ce.bin'     = '04027c3a7c875aaf581ca438ae5ff93ec90002e0e057dc107e7b47048331ee3c'
    'verde_me.bin'     = '7bcda47e186e575668e51a6fd02c086ad122f7d79e69f3b57a109b11fb11e073'
    'pitcairn_rlc.bin' = '929f722b65558862b24569f1397ba1f9db2ef301b66440533ebed0a2195ee92c'
    'pitcairn_pfp.bin' = 'aabdb871219fe135cb125c4e38adababdd27a02519324f5534abc00f46b24b5b'
    'pitcairn_ce.bin'  = '04027c3a7c875aaf581ca438ae5ff93ec90002e0e057dc107e7b47048331ee3c'
    'pitcairn_me.bin'  = '87617358bb876187786c422c3456fef1d072e6532382ecace0847259045131cb'
    'tahiti_rlc.bin'   = '46da3468097bb48a24ba47d0cc883695752d8e4f1d333c8a8bdb6e339894bac3'
    'tahiti_pfp.bin'   = 'aabdb871219fe135cb125c4e38adababdd27a02519324f5534abc00f46b24b5b'
    'tahiti_ce.bin'    = '04027c3a7c875aaf581ca438ae5ff93ec90002e0e057dc107e7b47048331ee3c'
    'tahiti_me.bin'    = '87617358bb876187786c422c3456fef1d072e6532382ecace0847259045131cb'
}
$licenseHash = '572872598565dc3513470de971a32bf9db301f47afeef3636345eadae33b2eee'

function Get-V9xFirmwarePath([string]$Name) {
    if ($Name.StartsWith('oland_')) {
        return Join-Path $FirmwareDirectory $Name
    }
    return Join-Path $SiFirmwareDirectory $Name
}

foreach ($name in $expected.Keys) {
    $path = Get-V9xFirmwarePath $name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing $path."
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $expected[$name]) {
        throw "$name has SHA-256 $hash, expected $($expected[$name])."
    }
}
foreach ($directory in @($FirmwareDirectory, $SiFirmwareDirectory)) {
    $path = Join-Path $directory 'LICENSE.amdgpu'
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $licenseHash) {
        throw "$path has SHA-256 $hash, expected $licenseHash."
    }
}

# The images emitted: name in C (v9x_si_<Name>_microcode), file, header
# major version Linux reads it with, and the dword count si_gfx_microcode.h
# declares (its V9X_SI_<Size>_MICROCODE_DWORDS).
$images = @(
    @{ Name = 'rlc';          File = 'oland_rlc.bin';    Size = 'RLC' },
    @{ Name = 'pfp';          File = 'oland_pfp.bin';    Size = 'PFP' },
    @{ Name = 'ce';           File = 'oland_ce.bin';     Size = 'CE' },
    @{ Name = 'me';           File = 'oland_me.bin';     Size = 'ME' },
    @{ Name = 'verde_rlc';    File = 'verde_rlc.bin';    Size = 'RLC' },
    @{ Name = 'verde_pfp';    File = 'verde_pfp.bin';    Size = 'PFP' },
    @{ Name = 'verde_me';     File = 'verde_me.bin';     Size = 'ME' },
    @{ Name = 'pitcairn_rlc'; File = 'pitcairn_rlc.bin'; Size = 'RLC' },
    @{ Name = 'pitcairn_pfp'; File = 'pitcairn_pfp.bin'; Size = 'PFP' },
    @{ Name = 'tahiti_rlc';   File = 'tahiti_rlc.bin';   Size = 'RLC' }
)
# The files not emitted, and the emitted file each is byte for byte.
$shared = [ordered]@{
    'verde_ce.bin'    = 'oland_ce.bin'
    'pitcairn_ce.bin' = 'oland_ce.bin'
    'pitcairn_me.bin' = 'oland_me.bin'
    'tahiti_pfp.bin'  = 'pitcairn_pfp.bin'
    'tahiti_ce.bin'   = 'oland_ce.bin'
    'tahiti_me.bin'   = 'oland_me.bin'
}
foreach ($name in $shared.Keys) {
    if ($expected[$name] -ne $expected[$shared[$name]]) {
        throw "$name is not $($shared[$name]); the sharing table is stale."
    }
}
# And no two emitted images are the same file.
$emittedHashes = @{}
foreach ($image in $images) {
    $hash = $expected[$image.File]
    if ($emittedHashes.ContainsKey($hash)) {
        throw "$($image.File) is $($emittedHashes[$hash]); share it instead."
    }
    $emittedHashes[$hash] = $image.File
}
$dwordsFor = @{ RLC = 2048; PFP = 2144; CE = 2144; ME = 2144 }

$license = Get-Content -LiteralPath (Join-Path $FirmwareDirectory 'LICENSE.amdgpu')
$output = @(
    '/*',
    ' * The AMD Southern Islands graphics engine microcode: the RLC''s and the',
    ' * CP''s PFP, CE and ME images Linux gfx_v6_0.c writes to each engine''s',
    ' * data port, as host-order dwords, and each image''s version for the',
    ' * diagnostics. The Oland''s, then the images of Cape Verde, Pitcairn',
    ' * and Tahiti that are not already here (si_gfx_microcode.h).',
    ' *',
    ' * Generated by scripts\gen-oland-gfx-microcode.ps1 from linux-firmware''s',
    ' * amdgpu/{oland,verde,pitcairn,tahiti}_{rlc,pfp,ce,me}.bin (SHA-256 in',
    ' * the script). The Oland''s ran on W9QI0DAD; the others have run on no',
    ' * card.',
    ' * Do not edit: regenerate from the .bin files.',
    ' *',
    ' * linux-firmware WHENCE: "Licence: Redistributable. See LICENSE.amdgpu',
    ' * for details." LICENSE.amdgpu, reproduced as its redistribution terms',
    ' * require:',
    ' *'
)
foreach ($line in $license) {
    $output += (' * ' + $line).TrimEnd()
}
$output += @(
    ' */',
    '#include "velocity9x/si_gfx_microcode.h"'
)

$summary = @()
foreach ($image in $images) {
    $bytes = [IO.File]::ReadAllBytes((Get-V9xFirmwarePath $image.File))
    $sizeBytes = [BitConverter]::ToUInt32($bytes, 0x00)
    $headerMajor = [BitConverter]::ToUInt16($bytes, 0x08)
    $ucodeVersion = [BitConverter]::ToUInt32($bytes, 0x10)
    $ucodeBytes = [BitConverter]::ToUInt32($bytes, 0x14)
    $arrayOffset = [BitConverter]::ToUInt32($bytes, 0x18)
    if ($sizeBytes -ne $bytes.Length -or $headerMajor -ne 1 -or
        ($ucodeBytes % 4) -ne 0 -or $ucodeBytes -eq 0 -or
        $arrayOffset + $ucodeBytes -gt $bytes.Length) {
        throw "$($image.File) has an inconsistent firmware header."
    }
    $dwords = $ucodeBytes / 4
    if ($dwords -ne $dwordsFor[$image.Size]) {
        throw "$($image.File) has $dwords dwords, si_gfx_microcode.h declares $($dwordsFor[$image.Size])."
    }
    $output += @(
        '',
        ('const v9x_u32 v9x_si_{0}_microcode_version = 0x{1:x8}ul;' -f $image.Name, $ucodeVersion),
        "const v9x_u32 v9x_si_$($image.Name)_microcode[V9X_SI_$($image.Size)_MICROCODE_DWORDS] = {"
    )
    for ($row = 0; $row -lt $dwords; $row += 4) {
        $cells = @()
        for ($column = 0; $column -lt 4 -and $row + $column -lt $dwords; ++$column) {
            $value = [BitConverter]::ToUInt32($bytes, $arrayOffset + ($row + $column) * 4)
            $cells += ('0x{0:x8}ul' -f $value)
        }
        $separator = if ($row + 4 -lt $dwords) { ',' } else { '' }
        $output += '    ' + ($cells -join ', ') + $separator
    }
    $output += '};'
    $summary += ('{0} {1} dwords, version 0x{2:x}' -f $image.Name, $dwords, $ucodeVersion)
}

$targetDirectory = Join-Path $repoRoot 'src\chipsets\ati\si'
if (-not (Test-Path -LiteralPath $targetDirectory)) {
    New-Item -ItemType Directory -Path $targetDirectory | Out-Null
}
$target = Join-Path $targetDirectory 'si_gfx_microcode.c'
[IO.File]::WriteAllText($target, (($output -join "`r`n") + "`r`n"), [Text.Encoding]::ASCII)
Write-Output "Wrote $target ($($summary -join '; '); $($shared.Count) shared)."
