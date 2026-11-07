[CmdletBinding()]
param(
    [string]$LinuxDirectory
)

# Generates src\chipsets\ati\cik\cik_gpu_init_tables.c: the AMD Mullins
# (Radeon R3E, GCN 2) register tables Linux writes before the graphics
# engine draws - cik.c's kalindi_mgcg_cgcg_init, godavari_golden_registers,
# kalindi_golden_common_registers and kalindi_golden_spm_registers (the
# CHIP_MULLINS case of cik_init_golden_registers, applied by
# amdgpu_device_program_register_sequence in that order), and the
# KABINI/KAVERI/MULLINS tile and macrotile tables of
# gfx_v7_0_tiling_mode_table_init. Both files are MIT (AMD); their notices go
# with the tables. Each must match the SHA-256 recorded here.
#
# cik.c's golden tables give dword indices, not names; the output keeps
# byte offsets (index times four). The tile tables' field macros
# (ARRAY_MODE(x) and the rest, cikd.h) resolve through gfx_7_2_sh_mask.h's
# shifts and gfx_7_2_enum.h's values. One tile entry depends on the DRAM
# row size, TILE_SPLIT(split_equal_to_row_size): it is generated for a
# 1 KiB row, gfx_v7_0_gpu_early_init's APU answer unless a DIMM's bank
# mapping is 11 (MC_FUS_DRAMn_BANK_ADDR_MAPPING, not read on M-JJN1ZZ).

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $LinuxDirectory) {
    $LinuxDirectory = Join-Path $repoRoot 'build\external\carrizo\linux\drivers\gpu\drm\amd'
}
$cikPath = Join-Path $LinuxDirectory 'amdgpu\cik.c'
$gfxPath = Join-Path $LinuxDirectory 'amdgpu\gfx_v7_0.c'
$headers = Join-Path $LinuxDirectory 'include\asic_reg\gca'
$expected = @{
    $cikPath = '8736c78b9f9f90aadf76a6d4e63d8e0f4d95bd51f86d657c54d893de0e66516c'
    $gfxPath = 'a69982da09d09d123ceb715675ee1595094acd3951295360d40d1acb2fed0c54'
}
foreach ($path in $expected.Keys) {
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $expected[$path]) {
        throw "$path has SHA-256 $hash, expected $($expected[$path])."
    }
}
$cik = [IO.File]::ReadAllText($cikPath)
$gfx = [IO.File]::ReadAllText($gfxPath)

$shifts = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $headers 'gfx_7_2_sh_mask.h'))) {
    $match = [regex]::Match($line, '^#define\s+(GB_(TILE|MACROTILE)_MODE0__\w+)__SHIFT\s+0x([0-9a-fA-F]+)')
    if ($match.Success) {
        $shifts[$match.Groups[1].Value] = [Convert]::ToInt32($match.Groups[3].Value, 16)
    }
}
$enums = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $headers 'gfx_7_2_enum.h'))) {
    $match = [regex]::Match($line, '^\s*(\w+)\s*=\s*0x([0-9a-fA-F]+)\s*,')
    if ($match.Success -and -not $enums.ContainsKey($match.Groups[1].Value)) {
        $enums[$match.Groups[1].Value] = [Convert]::ToUInt32($match.Groups[2].Value, 16)
    }
}
$enums['split_equal_to_row_size'] = $enums['ADDR_SURF_TILE_SPLIT_1KB']
$fieldShift = @{
    'ARRAY_MODE' = 'GB_TILE_MODE0__ARRAY_MODE'
    'PIPE_CONFIG' = 'GB_TILE_MODE0__PIPE_CONFIG'
    'TILE_SPLIT' = 'GB_TILE_MODE0__TILE_SPLIT'
    'MICRO_TILE_MODE_NEW' = 'GB_TILE_MODE0__MICRO_TILE_MODE_NEW'
    'SAMPLE_SPLIT' = 'GB_TILE_MODE0__SAMPLE_SPLIT'
    'BANK_WIDTH' = 'GB_MACROTILE_MODE0__BANK_WIDTH'
    'BANK_HEIGHT' = 'GB_MACROTILE_MODE0__BANK_HEIGHT'
    'MACRO_TILE_ASPECT' = 'GB_MACROTILE_MODE0__MACRO_TILE_ASPECT'
    'NUM_BANKS' = 'GB_MACROTILE_MODE0__NUM_BANKS'
}

function Get-V9xSequence([string]$Name) {
    $match = [regex]::Match($cik, "static const u32 $Name\[\]\s*=\s*\{(.*?)\};", 'Singleline')
    if (-not $match.Success) {
        throw "$Name not found in cik.c."
    }
    $rows = @()
    foreach ($row in [regex]::Matches($match.Groups[1].Value,
            '0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)')) {
        # godavari_golden_registers lists 260C0h with DB_DEBUG2's mask and
        # value (F00FFFFFh, 400h), most likely a typo for 260Dh: as written
        # it is byte 98300h, past the 256 KiB register BAR, which Linux's
        # WREG32 would reach indirectly through MM_INDEX. constants_init's
        # own DB_DEBUG2 read-modify-write sets the same bits, so the entry
        # is left out rather than guessed at. Any other entry past the BAR
        # stops the script.
        $offset = [Convert]::ToUInt32($row.Groups[1].Value, 16) * 4
        if ($offset -ge 0x40000) {
            if ($Name -eq 'godavari_golden_registers' -and $row.Groups[1].Value -eq '260c0') {
                continue
            }
            throw "$Name has an entry at byte $('{0:x}' -f $offset), past the register BAR."
        }
        $rows += @{
            Offset = [Convert]::ToUInt32($row.Groups[1].Value, 16) * 4
            Clear = [Convert]::ToUInt32($row.Groups[2].Value, 16)
            Set = [Convert]::ToUInt32($row.Groups[3].Value, 16)
        }
    }
    if ($rows.Count -eq 0) {
        throw "$Name has no entries."
    }
    return ,$rows
}

function Get-V9xFieldValue([string]$Expression) {
    $value = [uint32]0
    foreach ($term in [regex]::Matches($Expression, '(\w+)\((\w+)\)')) {
        $macro = $term.Groups[1].Value
        $argument = $term.Groups[2].Value
        if (-not $fieldShift.ContainsKey($macro) -or -not $shifts.ContainsKey($fieldShift[$macro])) {
            throw "Unknown field macro $macro."
        }
        if (-not $enums.ContainsKey($argument)) {
            throw "Unknown enum value $argument."
        }
        $value = $value -bor ([uint32]$enums[$argument] -shl $shifts[$fieldShift[$macro]])
    }
    return $value
}

# The case CHIP_KABINI / KAVERI / MULLINS / default of
# gfx_v7_0_tiling_mode_table_init: from its default label to the break.
$tileStart = $gfx.IndexOf('static void gfx_v7_0_tiling_mode_table_init')
$caseStart = $gfx.IndexOf('case CHIP_MULLINS:', $tileStart)
$defaultAt = $gfx.IndexOf('default:', $caseStart)
$caseEnd = $gfx.IndexOf('break;', $defaultAt)
if ($caseStart -lt 0 -or $defaultAt -lt 0 -or $caseEnd -lt 0 -or $defaultAt - $caseStart -gt 40) {
    throw 'The Mullins tiling case was not found where expected.'
}
$case = $gfx.Substring($defaultAt, $caseEnd - $defaultAt)
$modes = New-Object 'uint32[]' 32
$macros = New-Object 'uint32[]' 16
foreach ($assign in [regex]::Matches($case, '(tile|macrotile)\[(\d+)\]\s*=\s*\((.*?)\);', 'Singleline')) {
    $index = [int]$assign.Groups[2].Value
    $value = Get-V9xFieldValue $assign.Groups[3].Value
    if ($assign.Groups[1].Value -eq 'tile') {
        $modes[$index] = $value
    } else {
        $macros[$index] = $value
    }
}
if ($case -notmatch 'if \(reg_offset != 7\)\s*WREG32\(mmGB_MACROTILE_MODE0') {
    throw 'The Mullins tile loop no longer skips macrotile 7.'
}

$sequences = [ordered]@{
    'v9x_cik_kalindi_mgcg_cgcg_init' = Get-V9xSequence 'kalindi_mgcg_cgcg_init'
    'v9x_cik_godavari_golden' = Get-V9xSequence 'godavari_golden_registers'
    'v9x_cik_kalindi_golden_common' = Get-V9xSequence 'kalindi_golden_common_registers'
    'v9x_cik_kalindi_golden_spm' = Get-V9xSequence 'kalindi_golden_spm_registers'
}

function Get-V9xNotice([string]$Text) {
    $license = [regex]::Match($Text, '^/\*(.*?)\*/', 'Singleline').Groups[1].Value
    $lines = @()
    foreach ($line in ($license -split "`n")) {
        $clean = $line.TrimEnd("`r").TrimStart()
        if ($clean.StartsWith('*')) {
            $clean = $clean.Substring(1)
        }
        $lines += (' *' + $clean).TrimEnd()
    }
    return $lines
}

$output = @(
    '/*',
    ' * Mullins'' golden registers and tile tables: Linux cik.c''s',
    ' * kalindi_mgcg_cgcg_init, godavari_golden_registers,',
    ' * kalindi_golden_common_registers and kalindi_golden_spm_registers',
    ' * (cik_init_golden_registers, CHIP_MULLINS, in that order) and the',
    ' * KABINI/KAVERI/MULLINS tile/macrotile arrays of',
    ' * gfx_v7_0_tiling_mode_table_init (a 1 KiB DRAM row), as byte offsets',
    ' * and values.',
    ' *',
    ' * Generated by scripts\gen-mullins-gpu-init.ps1 from cik.c (SHA-256',
    " * $($expected[$cikPath])) and gfx_v7_0.c (SHA-256",
    " * $($expected[$gfxPath])).",
    ' * Do not edit: regenerate from the sources.',
    ' *',
    ' * cik.c''s notice, reproduced as its licence requires (gfx_v7_0.c''s is',
    ' * the same MIT text):',
    ' *'
)
$output += Get-V9xNotice $cik
$output += @(' */', '#include "velocity9x/cik_gpu_init.h"', '')
foreach ($symbol in $sequences.Keys) {
    $rows = $sequences[$symbol]
    $output += "const struct v9x_vi_reg_seq $symbol[$($rows.Count)] = {"
    for ($index = 0; $index -lt $rows.Count; ++$index) {
        $row = $rows[$index]
        $separator = if ($index + 1 -lt $rows.Count) { ',' } else { '' }
        $output += ('    {{ 0x{0:x5}ul, 0x{1:x8}ul, 0x{2:x8}ul }}{3}' -f
                    $row.Offset, $row.Clear, $row.Set, $separator)
    }
    $output += @('};', '')
}
$output += 'const v9x_u32 v9x_cik_mullins_tile_modes[32] = {'
for ($index = 0; $index -lt 32; $index += 4) {
    $separator = if ($index + 4 -lt 32) { ',' } else { '' }
    $output += ('    0x{0:x8}ul, 0x{1:x8}ul, 0x{2:x8}ul, 0x{3:x8}ul{4}' -f
                $modes[$index], $modes[$index + 1], $modes[$index + 2], $modes[$index + 3], $separator)
}
$output += @('};', '', 'const v9x_u32 v9x_cik_mullins_macrotile_modes[16] = {')
for ($index = 0; $index -lt 16; $index += 4) {
    $separator = if ($index + 4 -lt 16) { ',' } else { '' }
    $output += ('    0x{0:x8}ul, 0x{1:x8}ul, 0x{2:x8}ul, 0x{3:x8}ul{4}' -f
                $macros[$index], $macros[$index + 1], $macros[$index + 2], $macros[$index + 3], $separator)
}
$output += '};'

$target = Join-Path $repoRoot 'src\chipsets\ati\cik\cik_gpu_init_tables.c'
[IO.File]::WriteAllText($target, (($output -join "`r`n") + "`r`n"), [Text.Encoding]::ASCII)
$counts = ($sequences.Keys | ForEach-Object { "$_ $($sequences[$_].Count)" }) -join ', '
Write-Output "Wrote $target ($counts)."
