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
        # V9XUPD.EXE's protocol text handling; the network half is Win32.
        'src\common\update_proto.c',
        # SHA-256, SHA-512 and Ed25519 for the updater's signed release
        # file, and the file's own framing.
        'src\common\sha256.c',
        'src\common\sha512.c',
        'src\common\ed25519.c',
        'src\common\update_release.c',
        # Deflate, CRC-32 and the zip reader the updater unpacks with.
        'src\common\crc32.c',
        'src\common\inflate.c',
        'src\common\zipread.c',
        'src\common\d3dmode.c',
        'src\common\vsync.c',
        'src\common\vbe_crtc.c',
        'src\common\donewait.c',
        'src\common\drawnote.c',
        'src\common\diag_identity.c',
        'src\common\i9xx_cover.c',
        'src\common\i9xx_depth.c',
        'src\common\i9xx_wm.c',
        # PE32 export-by-ordinal walk over a bounded byte range; the HAL's
        # route to KERNEL32's Win16-mutex ordinals.
        'src\common\pe_export.c',
        # Shared Mach64 FIFO, wait, reset/replay and coherence policy.
        'src\chipsets\ati\mach64_engine.c',
        # Mach64 draw acceptance: the measured Phase 4 boundary.
        'src\chipsets\ati\mach64_policy.c',
        # Mach64 draw state and setup packets built on the proven builders.
        'src\chipsets\ati\mach64_draw.c',
        # Rage II (264GT2C) trapezoid register encoding: no setup engine.
        'src\chipsets\ati\mach64_crtc.c',
        'src\chipsets\ati\rage2_trap.c',
        # SiS 6326 2D engine: fill and copy register values, no I/O.
        'src\chipsets\sis\sis6326_engine.c',
        # SiS 6326 3D engine: one triangle's register values, no I/O.
        'src\chipsets\sis\sis6326_3d.c',
        # MGA-2064W drawing engine: fill and copy register values, no I/O.
        'src\chipsets\matrox\mga_engine.c',
        # Rage II triangle setup into trapezoids, on the measured edge walk.
        'src\chipsets\ati\rage2_setup.c',
        'src\chipsets\ati\rage2_draw.c',
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
        'src\chipsets\intel\i9xx_fog.c',
        'src\chipsets\intel\i9xx_multitex.c',
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
        'src\display32\r3d\r3d_records.c',
        'src\display32\r3d\r3d_cull.c',
        'src\display32\r3d\r3d_line.c',
        'src\display32\r3d\r3d_clear.c',
        'src\display32\r3d\r3d_validate.c',
        'src\opengl\gl_state.c',
        'src\opengl\gl_matrix.c',
        'src\opengl\gl_prim.c',
        'src\opengl\gl_texture.c',
        'src\opengl\gl_get.c',
        'src\opengl\gl_pixels.c',
        'src\opengl\gl_varray.c',
        'src\opengl\gl_attrib.c',
        # GLIDE2X.DLL's pure logic (docs\plans\glide-2x-wrapper.md, Phase 1).
        'src\glide\glide_vertex.c',
        'src\glide\glide_texmem.c',
        'src\glide\glide_texfmt.c',
        'src\glide\glide_state.c',
        # Direct3D render state to the neutral draw: the identity, asserted.
        'src\display32\d3d\d3d_state.c',
        'tests\host\test_d3d_state.c',
        # The DrawPrimitives2 command walker: record layouts and bounds.
        'src\display32\d3d\d3d_dp2.c',
        'tests\host\test_d3d_dp2.c',
        # Which D3D engine serves the chip, at publish time and every call.
        'src\display32\d3d\d3d_select.c',
        'tests\host\test_d3d_select.c',
        # The neutral draw to the Mach64 policy request and draw state.
        'src\display32\d3d\d3d_mach64_map.c',
        'tests\host\test_d3d_mach64_map.c',
        # The neutral draw to SiS 6326 state, texture and triangle words.
        'src\display32\d3d\d3d_sis6326_map.c',
        'tests\host\test_d3d_sis6326_map.c',
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
        'tests\host\test_update_proto.c',
        'tests\host\test_crypto.c',
        'tests\host\test_update_release.c',
        'tests\host\test_inflate.c',
        'tests\host\test_d3dmode.c',
        'tests\host\test_vsync.c',
        'tests\host\test_vbe_crtc.c',
        'tests\host\test_d3d_raster.c',
        'tests\host\test_r3d_clip.c',
        'tests\host\test_r3d_cull.c',
        'tests\host\test_r3d_line.c',
        'tests\host\test_r3d_clear.c',
        'tests\host\test_r3d_runs.c',
        'tests\host\test_r3d_records.c',
        'tests\host\test_r3d_validate.c',
        'tests\host\test_gl_state.c',
        'tests\host\test_gl_matrix.c',
        'tests\host\test_gl_prim.c',
        'tests\host\test_gl_texture.c',
        'tests\host\test_gl_get.c',
        'tests\host\test_gl_pixels.c',
        'tests\host\test_gl_varray.c',
        'tests\host\test_gl_attrib.c',
        'tests\host\test_glide_vertex.c',
        'tests\host\test_glide_texture.c',
        'tests\host\test_glide_state.c',
        'tests\host\test_donewait.c',
        'tests\host\test_drawnote.c',
        'tests\host\test_diag_identity.c',
        'tests\host\test_i9xx_cover.c',
        'tests\host\test_i9xx_depth.c',
        'tests\host\test_i9xx_wm.c',
        'tests\host\test_i9xx_mmio.c',
        'tests\host\test_i9xx_gtt.c',
        'tests\host\test_i9xx_ring.c',
        'tests\host\test_i9xx_blt.c',
        'tests\host\test_i9xx_arm.c',
        'tests\host\test_i9xx_3d.c',
        'tests\host\test_mach64_engine.c',
        'tests\host\test_mach64_policy.c',
        'tests\host\test_mach64_draw.c',
        'tests\host\test_mach64_crtc.c',
        'tests\host\test_rage2_trap.c',
        'tests\host\rage2_reference.c',
        'tests\host\test_rage2_setup.c',
        'tests\host\test_rage2_draw.c',
        'tests\host\test_sis6326_engine.c',
        'tests\host\test_sis6326_3d.c',
        'tests\host\test_mga_engine.c',
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
