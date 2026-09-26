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
        'src\common\drawnote.c',
        'src\common\i9xx_cover.c',
        'src\common\i9xx_depth.c',
        'src\common\i9xx_wm.c',
        # PE32 export-by-ordinal walk over a bounded byte range; the HAL's
        # route to KERNEL32's Win16-mutex ordinals.
        'src\common\pe_export.c',
        'tests\host\test_pe_export.c',
        # The generated OpenGL dispatch table (gl_dispatch_gen.h beside the
        # executable), asserted against the two reference headers' order.
        'tests\host\test_gl_dispatch.c',
        # Intel Gen3 read-only fingerprint decoding; no MMIO access here.
        'src\chipsets\intel\i9xx_mmio.c',
        # Scanline/frame-counter summary; the HAL reads, this counts.
        'src\chipsets\intel\i9xx_scanline.c',
        # The ring flip stream, built and decoded; the HAL submits it.
        'src\chipsets\intel\i9xx_flip.c',
        # Streaming GTT/PTE inventory; pure arithmetic, no MMIO access here.
        'src\chipsets\intel\i9xx_gtt.c',
        # Phase 4 sandbox/ring arithmetic and exact command allowlist.
        'src\chipsets\intel\i9xx_ring.c',
        # DirectDraw 2D blit builders and allowlist; the HAL submits them.
        'src\chipsets\intel\i9xx_blt.c',
        # One-shot Phase 4 arm contract and packet CRC.
        'src\chipsets\intel\i9xx_arm.c',
        'src\chipsets\intel\i9xx_float.c',
        'src\chipsets\intel\i9xx_3d.c',
        'src\chipsets\intel\i9xx_fragprog.c',
        'src\chipsets\intel\i9xx_vertex.c',
        'src\chipsets\intel\i9xx_3d_stream.c',
        # Phase 6 scene table and per-scene stream assembly.
        'src\chipsets\intel\i9xx_scene.c',
        'src\chipsets\intel\i9xx_texture.c',
        'src\chipsets\intel\i9xx_3d_decode.c',
        'src\chipsets\intel\i9xx_chain.c',
        # Pure rasterizer arithmetic: no DDHAL, MMIO or assembly dependencies.
        'src\display32\d3d\d3d_raster.c',
        # Back-face culling decision; the core applies it, no DDHAL here.
        # The neutral render core: screen-space clipping, the list builder
        # and the cull decision, shared by the D3D core and the OpenGL ICD.
        'src\display32\r3d\r3d_clip.c',
        'src\display32\r3d\r3d_cull.c',
        'src\display32\r3d\r3d_line.c',
        'src\display32\r3d\r3d_clear.c',
        'src\display32\r3d\r3d_validate.c',
        'src\opengl\gl_state.c',
        'src\opengl\gl_matrix.c',
        # Direct3D render state to the neutral draw: the identity, asserted.
        'src\display32\d3d\d3d_state.c',
        'tests\host\test_d3d_state.c',
        'src\display32\d3d\d3d_i9xx_target.c'
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
        'tests\host\test_r3d_clip.c',
        'tests\host\test_r3d_cull.c',
        'tests\host\test_r3d_line.c',
        'tests\host\test_r3d_clear.c',
        'tests\host\test_r3d_validate.c',
        'tests\host\test_gl_state.c',
        'tests\host\test_gl_matrix.c',
        'tests\host\test_donewait.c',
        'tests\host\test_drawnote.c',
        'tests\host\test_i9xx_cover.c',
        'tests\host\test_i9xx_depth.c',
        'tests\host\test_i9xx_wm.c',
        'tests\host\test_i9xx_mmio.c',
        'tests\host\test_i9xx_gtt.c',
        'tests\host\test_i9xx_ring.c',
        'tests\host\test_i9xx_blt.c',
        'tests\host\test_i9xx_arm.c',
        'tests\host\test_i9xx_3d.c',
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
