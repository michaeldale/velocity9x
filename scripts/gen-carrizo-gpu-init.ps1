[CmdletBinding()]
param(
    [string]$Source,
    [string]$HeaderDirectory
)

# Generates src\chipsets\ati\vi\vi_gpu_init_tables.c: the Carrizo register
# tables Linux v4.9 gfx_v8_0.c writes before the graphics engine draws -
# cz_mgcg_cgcg_init, cz_golden_settings_a11 and cz_golden_common_all
# (gfx_v8_0_init_golden_registers, applied by
# amdgpu_program_register_sequence), and the CHIP_CARRIZO tile and macrotile
# tables of gfx_v8_0_tiling_mode_table_init. gfx_v8_0.c is MIT (AMD); its
# notice goes with the tables. The source must match the SHA-256 recorded
# here.
#
# Register names resolve to byte offsets through the gca, oss, gmc and bif
# *_d.h headers (dword indices times four); the tile tables' field macros
# (ARRAY_MODE(x) and the rest, gfx_v8_0.c lines 61-69) through
# gfx_8_0_sh_mask.h's shifts and gfx_8_0_enum.h's values. A name that does
# not resolve stops the script.

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$external = Join-Path $repoRoot 'build\external\carrizo'
if (-not $Source) {
    $Source = Join-Path $external 'old\v4.9-gfx_v8_0.c'
}
if (-not $HeaderDirectory) {
    $HeaderDirectory = Join-Path $external 'linux\drivers\gpu\drm\amd\include\asic_reg'
}
$expected = '9ebbe3d4e8365c75ed1de7c97d8faf7541851755874b810c3f3d53a116b2a347'
$hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne $expected) {
    throw "v4.9-gfx_v8_0.c has SHA-256 $hash, expected $expected."
}
$text = [IO.File]::ReadAllText($Source)

# mm names, from every header that might hold one.
$registers = @{}
foreach ($header in @('gca\gfx_8_0_d.h', 'oss\oss_3_0_d.h', 'gmc\gmc_8_1_d.h', 'bif\bif_5_0_d.h')) {
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $HeaderDirectory $header))) {
        $match = [regex]::Match($line, '^#define\s+(mm\w+)\s+0x([0-9a-fA-F]+)')
        if ($match.Success -and -not $registers.ContainsKey($match.Groups[1].Value)) {
            $registers[$match.Groups[1].Value] = [Convert]::ToUInt32($match.Groups[2].Value, 16) * 4
        }
    }
}
$shifts = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $HeaderDirectory 'gca\gfx_8_0_sh_mask.h'))) {
    $match = [regex]::Match($line, '^#define\s+(GB_(TILE|MACROTILE)_MODE0__\w+)__SHIFT\s+0x([0-9a-fA-F]+)')
    if ($match.Success) {
        $shifts[$match.Groups[1].Value] = [Convert]::ToInt32($match.Groups[3].Value, 16)
    }
}
$enums = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $HeaderDirectory 'gca\gfx_8_0_enum.h'))) {
    $match = [regex]::Match($line, '^\s*(\w+)\s*=\s*0x([0-9a-fA-F]+)\s*,')
    if ($match.Success -and -not $enums.ContainsKey($match.Groups[1].Value)) {
        $enums[$match.Groups[1].Value] = [Convert]::ToUInt32($match.Groups[2].Value, 16)
    }
}
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
    $match = [regex]::Match($text, "static const u32 $Name\[\]\s*=\s*\{(.*?)\};", 'Singleline')
    if (-not $match.Success) {
        throw "$Name not found."
    }
    $rows = @()
    foreach ($row in [regex]::Matches($match.Groups[1].Value,
            '(mm\w+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)')) {
        $register = $row.Groups[1].Value
        if (-not $registers.ContainsKey($register)) {
            throw "$Name names $register, which no header defines."
        }
        $rows += @{
            Name = $register.Substring(2)
            Offset = $registers[$register]
            Clear = [Convert]::ToUInt32($row.Groups[2].Value, 16)
            Set = [Convert]::ToUInt32($row.Groups[3].Value, 16)
        }
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

# The CHIP_CARRIZO case of gfx_v8_0_tiling_mode_table_init: from its label
# to the next case or the switch's end.
$tileStart = $text.IndexOf('static void gfx_v8_0_tiling_mode_table_init')
$caseStart = $text.IndexOf('case CHIP_CARRIZO:', $tileStart)
$caseEnd = $text.IndexOf('break;', $caseStart)
$case = $text.Substring($caseStart, $caseEnd - $caseStart)
$modes = New-Object 'uint32[]' 32
$macros = New-Object 'uint32[]' 16
foreach ($assign in [regex]::Matches($case, '(modearray|mod2array)\[(\d+)\]\s*=\s*\((.*?)\);', 'Singleline')) {
    $index = [int]$assign.Groups[2].Value
    $value = Get-V9xFieldValue $assign.Groups[3].Value
    if ($assign.Groups[1].Value -eq 'modearray') {
        $modes[$index] = $value
    } else {
        $macros[$index] = $value
    }
}
if ($case -notmatch 'reg_offset != 7 && reg_offset != 12 && reg_offset != 17 &&\s*reg_offset != 23' -or
    $case -notmatch 'if \(reg_offset != 7\)\s*WREG32\(mmGB_MACROTILE_MODE0') {
    throw 'The Carrizo tile loop no longer skips modes 7, 12, 17, 23 and macrotile 7.'
}

$sequences = [ordered]@{
    'v9x_vi_cz_mgcg_cgcg_init' = Get-V9xSequence 'cz_mgcg_cgcg_init'
    'v9x_vi_cz_golden_settings' = Get-V9xSequence 'cz_golden_settings_a11'
    'v9x_vi_cz_golden_common' = Get-V9xSequence 'cz_golden_common_all'
}

$license = [regex]::Match($text, '^/\*(.*?)\*/', 'Singleline').Groups[1].Value
$output = @(
    '/*',
    ' * Carrizo''s golden registers and tile tables: Linux v4.9 gfx_v8_0.c''s',
    ' * cz_mgcg_cgcg_init, cz_golden_settings_a11 and cz_golden_common_all',
    ' * (applied by amdgpu_program_register_sequence, in that order) and the',
    ' * CHIP_CARRIZO modearray/mod2array of gfx_v8_0_tiling_mode_table_init,',
    ' * as byte offsets and values.',
    ' *',
    ' * Generated by scripts\gen-carrizo-gpu-init.ps1 from gfx_v8_0.c',
    " * (SHA-256 $hash).",
    ' * Do not edit: regenerate from the source.',
    ' *',
    ' * gfx_v8_0.c''s notice, reproduced as its licence requires:',
    ' *'
)
foreach ($line in ($license -split "`n")) {
    $clean = $line.TrimEnd("`r").TrimStart()
    if ($clean.StartsWith('*')) {
        $clean = $clean.Substring(1)
    }
    $output += (' *' + $clean).TrimEnd()
}
$output += @(' */', '#include "velocity9x/vi_gpu_init.h"', '')
foreach ($symbol in $sequences.Keys) {
    $rows = $sequences[$symbol]
    $output += "const struct v9x_vi_reg_seq $symbol[$($rows.Count)] = {"
    for ($index = 0; $index -lt $rows.Count; ++$index) {
        $row = $rows[$index]
        $separator = if ($index + 1 -lt $rows.Count) { ',' } else { '' }
        $output += ('    {{ 0x{0:x5}ul, 0x{1:x8}ul, 0x{2:x8}ul }}{3} /* {4} */' -f
                    $row.Offset, $row.Clear, $row.Set, $separator, $row.Name)
    }
    $output += @('};', '')
}
$output += 'const v9x_u32 v9x_vi_cz_tile_modes[32] = {'
for ($index = 0; $index -lt 32; $index += 4) {
    $separator = if ($index + 4 -lt 32) { ',' } else { '' }
    $output += ('    0x{0:x8}ul, 0x{1:x8}ul, 0x{2:x8}ul, 0x{3:x8}ul{4}' -f
                $modes[$index], $modes[$index + 1], $modes[$index + 2], $modes[$index + 3], $separator)
}
$output += @('};', '', 'const v9x_u32 v9x_vi_cz_macrotile_modes[16] = {')
for ($index = 0; $index -lt 16; $index += 4) {
    $separator = if ($index + 4 -lt 16) { ',' } else { '' }
    $output += ('    0x{0:x8}ul, 0x{1:x8}ul, 0x{2:x8}ul, 0x{3:x8}ul{4}' -f
                $macros[$index], $macros[$index + 1], $macros[$index + 2], $macros[$index + 3], $separator)
}
$output += '};'

$target = Join-Path $repoRoot 'src\chipsets\ati\vi\vi_gpu_init_tables.c'
[IO.File]::WriteAllText($target, (($output -join "`r`n") + "`r`n"), [Text.Encoding]::ASCII)
$counts = ($sequences.Keys | ForEach-Object { "$_ $($sequences[$_].Count)" }) -join ', '
Write-Output "Wrote $target ($counts; source SHA-256 $hash)."
