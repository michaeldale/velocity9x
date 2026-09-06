# One source list for both host compilers. Chipset sources remain derived from
# the family manifests; the only compiler-specific group is Watcom's x87 code.

function Get-V9xHostSourceNames {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][ValidateSet('Watcom', 'MSVC')]
        [string]$Compiler
    )

    . (Join-Path $PSScriptRoot 'family.ps1')
    $backendSourceNames = @(Get-V9xFamilies -RepoRoot $RepoRoot |
        ForEach-Object { @($_.Backend.Sources) } | Sort-Object -Unique)

    $sourceNames = @(
        'src\common\build.c',
        'src\common\backend_registry.c',
        'src\common\mode.c',
        'src\common\log.c',
        'src\common\resources.c',
        'src\common\vbe_parse.c',
        'src\common\vbe_modes.c',
        'src\common\vbe_cache.c',
        'src\common\edid.c',
        'src\common\mtrr.c',
        'src\common\d3dmode.c',
        'src\common\vbe_crtc.c',
        'src\common\donewait.c',
        # Pure rasterizer arithmetic: no DDHAL, MMIO or assembly dependencies.
        'src\display32\d3d\d3d_raster.c'
    ) + $backendSourceNames + @(
        'src\display16\display_component.c',
        'src\minivdd32\minivdd_component.c',
        'tests\host\test_family_matrix.c',
        'tests\host\test_hw16_modes.c',
        'tests\host\test_vbe_parse.c',
        'tests\host\test_vbe_modes.c',
        'tests\host\test_vbe_cache.c',
        'tests\host\test_edid.c',
        'tests\host\test_mtrr.c',
        'tests\host\test_d3dmode.c',
        'tests\host\test_vbe_crtc.c',
        'tests\host\test_d3d_raster.c',
        'tests\host\test_donewait.c',
        'tests\host\test_main.c'
    )

    if ($Compiler -eq 'Watcom') {
        # Tests the actual #pragma aux fistp converter shipped in the HAL.
        # test_main.c gates this group on __WATCOMC__ and reports the MSVC skip.
        $sourceNames += @(
            'src\display32\d3d\d3d_zfixed.c',
            'tests\host\test_d3d_zfixed.c'
        )
    }
    return $sourceNames
}
