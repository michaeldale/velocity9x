[CmdletBinding()]
param(
    [string]$LinuxDirectory
)

# Generates src\chipsets\ati\si\si_gpu_init_tables.c: the AMD Southern
# Islands (GCN 1) register tables Linux writes before the graphics engine
# draws, for the Oland, Cape Verde, Pitcairn and Tahiti - amdgpu si.c's
# <chip>_golden_registers, <chip>_golden_rlc_registers and
# <chip>_mgcg_cgcg_init (si_init_golden_registers, applied by
# amdgpu_device_program_register_sequence in that order), and the tile
# tables of gfx_v6_0_tiling_mode_table_init (CHIP_OLAND's, CHIP_VERDE's,
# and the one CHIP_TAHITI and CHIP_PITCAIRN share). Both files are MIT
# (AMD); si.c's notice goes with the tables. Each source must match the
# SHA-256 recorded here.
#
# si_init_golden_registers applies a fourth list for two chips:
# tahiti_golden_registers2 (MCIF_MEM_CONTROL) and verde_pg_init (GMCON's
# power-gating state machine). Neither has an entry in the graphics block,
# which is asserted below; they are named in the output as left out.
#
# The golden tables give register names or dword indices; names resolve
# through amdgpu's asic_reg headers for SI, and the output keeps byte
# offsets. Only the graphics block's entries are kept (the windows below,
# which are SICP.VXD's table windows too): display, audio, the memory
# controller, VM, the HDP and the low indexed ports are left out, as
# Mullins' list left out its PCIe and SMC pairs, and each one left out is
# named in the output. The tile table's field macros resolve as
# gfx_v6_0.c defines them (through gfx_6_0_sh_mask.h's shifts) and their
# values through gfx_7_2_enum.h, which gfx_v6_0.c includes for them.
#
# Some tile entries depend on the DRAM row, TILE_SPLIT
# (split_equal_to_row_size). The tables are emitted for a 4 KiB row, the
# Oland's (W9QI0DAD's MC_ARB_RAMCFG 000012A1h: NOOFCOLS 2, 4 * 2^(8+2)
# bytes), and each table's row-dependent modes are emitted as a bit mask,
# so si_gpu_init.c can put the row the card reports in their split, as
# gfx_v6_0_constants_init computes it.

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $LinuxDirectory) {
    $LinuxDirectory = Join-Path $repoRoot 'build\external\carrizo\linux\drivers\gpu\drm'
}
$siPath = Join-Path $LinuxDirectory 'amd\amdgpu\si.c'
$gfxPath = Join-Path $LinuxDirectory 'amd\amdgpu\gfx_v6_0.c'
$headers = Join-Path $LinuxDirectory 'amd\include\asic_reg'
$expected = @{
    $siPath = '7f6c0e6d6dd5c8986c297ec8884044904b76fce8771756f2a52faaf1c8f3e13d'
    $gfxPath = '28aff5dbe04fb58f84d097c20e084169dacae8c965bd8e92b968de0c7646462f'
}
foreach ($path in $expected.Keys) {
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $expected[$path]) {
        throw "$path has SHA-256 $hash, expected $($expected[$path])."
    }
}
$si = [IO.File]::ReadAllText($siPath)
$gfx = [IO.File]::ReadAllText($gfxPath)

# Register names: dword indices in the SI headers si.c includes.
$registers = @{}
foreach ($file in @('gca\gfx_6_0_d.h', 'oss\oss_1_0_d.h', 'gmc\gmc_6_0_d.h',
                    'dce\dce_6_0_d.h', 'bif\bif_3_0_d.h', 'smu\smu_6_0_d.h')) {
    $path = Join-Path $headers $file
    if (-not (Test-Path -LiteralPath $path)) {
        continue
    }
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        $match = [regex]::Match($line, '^#define\s+mm(\w+)\s+0x([0-9a-fA-F]+)')
        if ($match.Success -and -not $registers.ContainsKey($match.Groups[1].Value)) {
            $registers[$match.Groups[1].Value] = [Convert]::ToUInt32($match.Groups[2].Value, 16)
        }
    }
}

# The graphics block, byte offsets [start, limit): SICP.VXD's
# SCInitWindows. GRBM_SOFT_RESET (8020h) and the RLC's microcode port
# (C32Ch-C333h) are outside on purpose.
$windows = @(
    @(0x8000, 0x8020), @(0x8024, 0xc000), @(0xc300, 0xc32c),
    @(0xc334, 0xc500), @(0x28000, 0x29000)
)
function Test-V9xGraphics([uint32]$Offset) {
    foreach ($window in $windows) {
        if ($Offset -ge $window[0] -and $Offset -lt $window[1]) {
            return $true
        }
    }
    return $false
}

$dropped = @()
function Get-V9xSequence([string]$Name, [bool]$AllowEmpty) {
    $match = [regex]::Match($si, "static const u32 $Name\[\]\s*=\s*\{(.*?)\};", 'Singleline')
    if (-not $match.Success) {
        throw "$Name not found in si.c."
    }
    $rows = @()
    $seen = 0
    foreach ($row in [regex]::Matches($match.Groups[1].Value,
            '(mm\w+|0x[0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)')) {
        ++$seen
        $token = $row.Groups[1].Value
        if ($token.StartsWith('mm')) {
            $short = $token.Substring(2)
            # The video decoder's registers (si.c includes uvd_4_0_d.h,
            # which the cached tree does not carry): the UVD block, never
            # graphics. tahiti_golden_rlc_registers names one,
            # UVD_CGC_GATE.
            if ($short.StartsWith('UVD_') -and -not $registers.ContainsKey($short)) {
                $script:dropped += ('{0} {1} (the UVD block)' -f $Name, $token)
                continue
            }
            if (-not $registers.ContainsKey($short)) {
                throw "$Name names $token, which no SI header defines."
            }
            $index = $registers[$short]
        } else {
            $index = [Convert]::ToUInt32($token.Substring(2), 16)
        }
        $offset = [uint32]($index * 4)
        if (-not (Test-V9xGraphics $offset)) {
            $script:dropped += ('{0} {1} (byte {2:x5}h)' -f $Name, $token, $offset)
            continue
        }
        $rows += @{
            Offset = $offset
            Clear = [Convert]::ToUInt32($row.Groups[2].Value, 16)
            Set = [Convert]::ToUInt32($row.Groups[3].Value, 16)
        }
    }
    if ($seen -eq 0) {
        throw "$Name has no entries."
    }
    if ($AllowEmpty) {
        if ($rows.Count -ne 0) {
            throw "$Name now has graphics entries, which si_gpu_init.c does not apply."
        }
        # One line for the whole list, not one an entry.
        $keep = $script:dropped.Count - $seen
        $script:dropped = @($script:dropped | Select-Object -First $keep)
        $script:dropped += ('{0}: the whole list ({1} entries)' -f $Name, $seen)
    } elseif ($rows.Count -eq 0) {
        throw "$Name has no graphics entries."
    }
    return ,$rows
}

# The tile field macros as gfx_v6_0.c defines them (a shift by number or
# through gfx_6_0_sh_mask.h), and their values from gfx_7_2_enum.h, the
# enum header gfx_v6_0.c includes.
$shifts = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $headers 'gca\gfx_6_0_sh_mask.h'))) {
    $match = [regex]::Match($line, '^#define\s+(\w+__SHIFT)\s+0x([0-9a-fA-F]+)')
    if ($match.Success) {
        $shifts[$match.Groups[1].Value] = [Convert]::ToInt32($match.Groups[2].Value, 16)
    }
}
$macroShift = @{}
foreach ($line in ($gfx -split "`n")) {
    $macro = [regex]::Match($line, '^#define\s+(\w+)\(x\)\s+\(\(x\)\s*<<\s*(\w+)\)')
    if (-not $macro.Success) {
        continue
    }
    $shift = $macro.Groups[2].Value
    if ($shift -match '^\d+$') {
        $macroShift[$macro.Groups[1].Value] = [int]$shift
    } elseif ($shifts.ContainsKey($shift)) {
        $macroShift[$macro.Groups[1].Value] = $shifts[$shift]
    } else {
        throw "gfx_v6_0.c's $($macro.Groups[1].Value) shifts by unknown $shift."
    }
}
$values = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $headers 'gca\gfx_7_2_enum.h'))) {
    $match = [regex]::Match($line, '^\s*(\w+)\s*=\s*0x([0-9a-fA-F]+)\s*,')
    if ($match.Success -and -not $values.ContainsKey($match.Groups[1].Value)) {
        $values[$match.Groups[1].Value] = [Convert]::ToUInt32($match.Groups[2].Value, 16)
    }
}
$values['split_equal_to_row_size'] = $values['ADDR_SURF_TILE_SPLIT_4KB']
$tileMacros = @('MICRO_TILE_MODE', 'ARRAY_MODE', 'PIPE_CONFIG', 'TILE_SPLIT',
                'BANK_WIDTH', 'BANK_HEIGHT', 'MACRO_TILE_ASPECT', 'NUM_BANKS')
foreach ($name in $tileMacros) {
    if (-not $macroShift.ContainsKey($name)) {
        throw "radeon sid.h does not define $name(x)."
    }
}

$tileStart = $gfx.IndexOf('static void gfx_v6_0_tiling_mode_table_init')
if ($tileStart -lt 0) {
    throw 'gfx_v6_0_tiling_mode_table_init was not found.'
}
# One case of the tiling function, from its opening line to its write loop:
# the 32 modes, and the modes whose split is the row's as a bit mask.
function Get-V9xTileCase([string]$Opening, [string]$Label) {
    $caseStart = $gfx.IndexOf($Opening, $tileStart)
    if ($caseStart -lt 0) {
        throw "The $Label tiling case was not found where expected."
    }
    $caseEnd = $gfx.IndexOf('for (reg_offset = 0; reg_offset < num_tile_mode_states', $caseStart)
    if ($caseEnd -lt 0) {
        throw "The $Label tiling case has no write loop."
    }
    $case = $gfx.Substring($caseStart, $caseEnd - $caseStart)
    $modes = New-Object 'uint32[]' 32
    $rowModes = [uint32]0
    $seen = 0
    foreach ($assign in [regex]::Matches($case, 'tilemode\[(\d+)\]\s*=(.*?);', 'Singleline')) {
        $index = [int]$assign.Groups[1].Value
        $value = [uint32]0
        foreach ($term in [regex]::Matches($assign.Groups[2].Value, '(\w+)\((\w+)\)')) {
            $macro = $term.Groups[1].Value
            $argument = $term.Groups[2].Value
            if (-not $macroShift.ContainsKey($macro) -or $tileMacros -notcontains $macro) {
                throw "$Label tilemode[$index] uses unknown macro $macro."
            }
            if (-not $values.ContainsKey($argument)) {
                throw "$Label tilemode[$index] uses unknown value $argument."
            }
            if ($argument -eq 'split_equal_to_row_size') {
                if ($macro -ne 'TILE_SPLIT') {
                    throw "$Label tilemode[$index] puts the row split in $macro."
                }
                $rowModes = $rowModes -bor ([uint32]1 -shl $index)
            }
            $value = $value -bor ([uint32]$values[$argument] -shl $macroShift[$macro])
        }
        $modes[$index] = $value
        ++$seen
    }
    if ($seen -eq 0) {
        throw "The $Label tiling case assigns nothing."
    }
    return @{ Modes = $modes; RowModes = $rowModes; Count = $seen }
}

# Per chip, in si_init_golden_registers' order: the three lists applied,
# then the fourth one, which must hold nothing for the graphics block.
$chips = [ordered]@{
    'oland' = @{
        Lists = @('oland_golden_registers', 'oland_golden_rlc_registers',
                  'oland_mgcg_cgcg_init')
        Extra = @()
    }
    'verde' = @{
        Lists = @('verde_golden_registers', 'verde_golden_rlc_registers',
                  'verde_mgcg_cgcg_init')
        Extra = @('verde_pg_init')
    }
    'pitcairn' = @{
        Lists = @('pitcairn_golden_registers', 'pitcairn_golden_rlc_registers',
                  'pitcairn_mgcg_cgcg_init')
        Extra = @()
    }
    'tahiti' = @{
        Lists = @('tahiti_golden_registers', 'tahiti_golden_rlc_registers',
                  'tahiti_mgcg_cgcg_init')
        Extra = @('tahiti_golden_registers2')
    }
}
$suffixes = @('golden', 'golden_rlc', 'mgcg_cgcg_init')
$sequences = [ordered]@{}
foreach ($chip in $chips.Keys) {
    for ($index = 0; $index -lt 3; ++$index) {
        $symbol = "v9x_si_${chip}_$($suffixes[$index])"
        $sequences[$symbol] = Get-V9xSequence $chips[$chip].Lists[$index] $false
    }
    foreach ($extra in $chips[$chip].Extra) {
        $null = Get-V9xSequence $extra $true
    }
}

$tiles = [ordered]@{
    'oland' = Get-V9xTileCase '} else if (adev->asic_type == CHIP_OLAND) {' 'Oland'
    'verde' = Get-V9xTileCase 'if (adev->asic_type == CHIP_VERDE) {' 'Cape Verde'
    'tahiti' = Get-V9xTileCase '} else if ((adev->asic_type == CHIP_TAHITI) || (adev->asic_type == CHIP_PITCAIRN)) {' 'Tahiti and Pitcairn'
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
    ' * The Southern Islands golden registers and tile tables: amdgpu si.c''s',
    ' * <chip>_golden_registers, <chip>_golden_rlc_registers and',
    ' * <chip>_mgcg_cgcg_init (si_init_golden_registers, in that order) for',
    ' * the Oland, Cape Verde (verde), Pitcairn and Tahiti, graphics entries',
    ' * only, and gfx_v6_0_tiling_mode_table_init''s CHIP_OLAND, CHIP_VERDE',
    ' * and CHIP_TAHITI/CHIP_PITCAIRN tile tables for a 4 KiB DRAM row, each',
    ' * with the mask of its modes whose split is the row''s, as byte offsets',
    ' * and values.',
    ' *',
    ' * Generated by scripts\gen-oland-gpu-init.ps1 from si.c (SHA-256',
    " * $($expected[$siPath])) and gfx_v6_0.c (SHA-256",
    " * $($expected[$gfxPath])), with amdgpu's SI register headers.",
    ' * Do not edit: regenerate from the sources.',
    ' *',
    ' * Left out, outside the graphics block:'
)
foreach ($entry in $dropped) {
    $output += " *   $entry"
}
$output += @(
    ' *',
    ' * si.c''s notice, reproduced as its licence requires (gfx_v6_0.c''s is',
    ' * the same MIT text):',
    ' *'
)
$output += Get-V9xNotice $si
$output += @(' */', '#include "velocity9x/si_gpu_init.h"', '')
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
$tileNames = @($tiles.Keys)
for ($table = 0; $table -lt $tileNames.Count; ++$table) {
    $chip = $tileNames[$table]
    $modes = $tiles[$chip].Modes
    $output += "const v9x_u32 v9x_si_${chip}_tile_modes[32] = {"
    for ($index = 0; $index -lt 32; $index += 4) {
        $separator = if ($index + 4 -lt 32) { ',' } else { '' }
        $output += ('    0x{0:x8}ul, 0x{1:x8}ul, 0x{2:x8}ul, 0x{3:x8}ul{4}' -f
                    $modes[$index], $modes[$index + 1], $modes[$index + 2], $modes[$index + 3], $separator)
    }
    $output += '};'
    $output += ('const v9x_u32 v9x_si_{0}_row_split_modes = 0x{1:x8}ul;' -f $chip, $tiles[$chip].RowModes)
    if ($table + 1 -lt $tileNames.Count) {
        $output += ''
    }
}

$target = Join-Path $repoRoot 'src\chipsets\ati\si\si_gpu_init_tables.c'
[IO.File]::WriteAllText($target, (($output -join "`r`n") + "`r`n"), [Text.Encoding]::ASCII)
$counts = ($sequences.Keys | ForEach-Object { "$_ $($sequences[$_].Count)" }) -join ', '
$tileCounts = ($tiles.Keys | ForEach-Object { '{0} {1} modes, row mask {2:x8}' -f $_, $tiles[$_].Count, $tiles[$_].RowModes }) -join ', '
Write-Output "Wrote $target ($counts; $tileCounts; $($dropped.Count) entries left out)."
