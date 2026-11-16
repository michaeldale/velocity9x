[CmdletBinding()]
param(
    [string]$LinuxDirectory
)

# Generates src\chipsets\ati\si\si_gpu_init_tables.c: the AMD Oland (GCN 1)
# register tables Linux writes before the graphics engine draws -
# amdgpu si.c's oland_golden_registers, oland_golden_rlc_registers and
# oland_mgcg_cgcg_init (si_init_golden_registers, CHIP_OLAND, applied by
# amdgpu_device_program_register_sequence in that order), and the
# CHIP_OLAND tile table of gfx_v6_0_tiling_mode_table_init. Both files are
# MIT (AMD); si.c's notice goes with the tables. Each source must match
# the SHA-256 recorded here.
#
# The golden tables give register names or dword indices; names resolve
# through amdgpu's asic_reg headers for SI, and the output keeps byte
# offsets. Only the graphics block's entries are kept (the windows below,
# which are SICP.VXD's table windows too): display, audio, the memory
# controller, VM, the HDP and the low indexed ports are left out, as
# Mullins' list left out its PCIe and SMC pairs, and each one left out is
# named in the output. The tile table's field macros resolve as
# gfx_v6_0.c defines them (through gfx_6_0_sh_mask.h's shifts) and their
# values through gfx_7_2_enum.h, which gfx_v6_0.c includes for them. One
# tile entry depends on the DRAM row, TILE_SPLIT
# (split_equal_to_row_size): 4 KiB, from W9QI0DAD's MC_ARB_RAMCFG
# 000012A1h (NOOFCOLS 2: 4 * 2^(8+2) bytes), as gfx_v6_0_constants_init
# computes it.

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
function Get-V9xSequence([string]$Name) {
    $match = [regex]::Match($si, "static const u32 $Name\[\]\s*=\s*\{(.*?)\};", 'Singleline')
    if (-not $match.Success) {
        throw "$Name not found in si.c."
    }
    $rows = @()
    foreach ($row in [regex]::Matches($match.Groups[1].Value,
            '(mm\w+|0x[0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)')) {
        $token = $row.Groups[1].Value
        if ($token.StartsWith('mm')) {
            $short = $token.Substring(2)
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
    if ($rows.Count -eq 0) {
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
$caseStart = $gfx.IndexOf('} else if (adev->asic_type == CHIP_OLAND) {', $tileStart)
$caseEnd = $gfx.IndexOf('for (reg_offset = 0; reg_offset < num_tile_mode_states', $caseStart)
if ($tileStart -lt 0 -or $caseStart -lt 0 -or $caseEnd -lt 0) {
    throw 'The Oland tiling case was not found where expected.'
}
$case = $gfx.Substring($caseStart, $caseEnd - $caseStart)
$modes = New-Object 'uint32[]' 32
$seen = 0
foreach ($assign in [regex]::Matches($case, 'tilemode\[(\d+)\]\s*=(.*?);', 'Singleline')) {
    $index = [int]$assign.Groups[1].Value
    $value = [uint32]0
    foreach ($term in [regex]::Matches($assign.Groups[2].Value, '(\w+)\((\w+)\)')) {
        $macro = $term.Groups[1].Value
        $argument = $term.Groups[2].Value
        if (-not $macroShift.ContainsKey($macro) -or $tileMacros -notcontains $macro) {
            throw "tilemode[$index] uses unknown macro $macro."
        }
        if (-not $values.ContainsKey($argument)) {
            throw "tilemode[$index] uses unknown value $argument."
        }
        $value = $value -bor ([uint32]$values[$argument] -shl $macroShift[$macro])
    }
    $modes[$index] = $value
    ++$seen
}
if ($seen -eq 0) {
    throw 'The Oland tiling case assigns nothing.'
}

$sequences = [ordered]@{
    'v9x_si_oland_golden' = Get-V9xSequence 'oland_golden_registers'
    'v9x_si_oland_golden_rlc' = Get-V9xSequence 'oland_golden_rlc_registers'
    'v9x_si_oland_mgcg_cgcg_init' = Get-V9xSequence 'oland_mgcg_cgcg_init'
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
    ' * Oland''s golden registers and tile table: amdgpu si.c''s',
    ' * oland_golden_registers, oland_golden_rlc_registers and',
    ' * oland_mgcg_cgcg_init (si_init_golden_registers, CHIP_OLAND, in that',
    ' * order), graphics entries only, and the CHIP_OLAND tile table of',
    ' * gfx_v6_0_tiling_mode_table_init (a 4 KiB DRAM row), as byte offsets',
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
$output += 'const v9x_u32 v9x_si_oland_tile_modes[32] = {'
for ($index = 0; $index -lt 32; $index += 4) {
    $separator = if ($index + 4 -lt 32) { ',' } else { '' }
    $output += ('    0x{0:x8}ul, 0x{1:x8}ul, 0x{2:x8}ul, 0x{3:x8}ul{4}' -f
                $modes[$index], $modes[$index + 1], $modes[$index + 2], $modes[$index + 3], $separator)
}
$output += '};'

$target = Join-Path $repoRoot 'src\chipsets\ati\si\si_gpu_init_tables.c'
[IO.File]::WriteAllText($target, (($output -join "`r`n") + "`r`n"), [Text.Encoding]::ASCII)
$counts = ($sequences.Keys | ForEach-Object { "$_ $($sequences[$_].Count)" }) -join ', '
Write-Output "Wrote $target ($counts; $seen tile modes; $($dropped.Count) entries left out)."
