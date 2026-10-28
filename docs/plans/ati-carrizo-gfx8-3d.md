# Direct3D on AMD Carrizo (Radeon R7E, GCN 3)

Date: 2026-10-28

Status: Phase 1 done: the RLC, PFP, CE and ME take AMD's microcode
through their own ports, and the CP runs Linux's ring test and the start
preamble from a ring in the carve-out, three runs of three
([record](../decisions/2026-10-28-carrizo-cp-ring-test.md)). Phase 2 in
part: the full clear state runs; a first solid rectangle (Phase 3,
host-tested GCN 3 shaders and streams) completes and writes nothing, and
the BIOS was found to leave the tile table unprogrammed
([record](../decisions/2026-10-28-carrizo-first-draw-attempt.md)).
`gfx_v8_0_gpu_init` and the golden registers now run before the draw and
read back as written ([record](../decisions/2026-10-28-carrizo-gpu-init.md)).
Phase 3 done: the solid rectangle draws pixel-exact once the SQ caches are
invalidated before the draw and the VS stops reading two scalar operands
in one instruction ([record](../decisions/2026-10-28-carrizo-first-rectangle.md)).
Phase 4 step 1 done: Gouraud triangles from TL vertices, every pixel
within +-2 ([record](../decisions/2026-10-28-carrizo-gouraud.md)). Step 2
done: Z16 depth, 1D-tiled, LESS with writes, colour and Z buffer exact
([record](../decisions/2026-10-28-carrizo-depth.md)). Step 3 done, and
with it Phase 4: an A8R8G8B8 texture through a T# and S#, copy and
modulate exact ([record](../decisions/2026-10-28-carrizo-texture-modulate.md)).
Phase 5 step 1 done: V9XD32 through the HAL passes everything but
blending and ARGB1555 at 16 and 32 bpp
([record](../decisions/2026-10-28-carrizo-d3d-hal-first-light.md)). Next:
step 2, blending and the alpha test. Parent plan:
[ati-carrizo-gfx8.md](ati-carrizo-gfx8.md). 2D runs on SDMA0
([record](../decisions/2026-10-28-carrizo-directdraw-on-sdma.md)), and
flips on D1 ([record](../decisions/2026-10-28-carrizo-page-flips.md)).
Michael asked for 3D next (2026-10-28).

## What it takes, against Cedar

[Cedar's 3D plan](ati-evergreen-cedar-3d.md) is the template: the CP runs
AMD microcode, fixed-function Direct3D becomes shader variants, and the
pipeline state goes in as PM4 packets. What is different on Carrizo:

1. **Four images, not two.** The graphics CP has three microengines (PFP,
   ME, CE) and the RLC must run before them. Linux v4.9 `gfx_v8_0.c` loads
   all four through their own ports when `smc_load_fw=0`
   (`gfx_v8_0_rlc_load_microcode`, `gfx_v8_0_cp_gfx_load_microcode`), the
   path SDMA0 took (the [write probe](../decisions/2026-10-28-carrizo-sdma-write-probe.md)).
   linux-firmware `amdgpu/carrizo_{pfp,me,ce}.bin` (common header 1.0;
   4,192, 4,192 and 2,144 dwords; versions E1h, 97h, 81h) and
   `carrizo_rlc.bin` (header 2.0, 3,072 dwords, version 9Ch), all at
   payload offset 100h. The compute engines' MEC is not needed for the
   graphics ring.
2. **The RLC's start.** `gfx_v8_0_rlc_resume`: stop the RLC, turn off
   coarse-grain clock gating and light sleep (`RLC_CGCG_CGLS_CTRL` reads
   3Fh as the BIOS left it), power gating off, an RLC soft reset, the
   microcode, start. Power-gating setup (`gfx_v8_0_init_pg`, the CP jump
   tables Carrizo hands the RLC) runs only when power gating is enabled,
   and it is left off here.
3. **No ME_INITIALIZE.** `gfx_v8_0_cp_gfx_start` sends a clear-state
   preamble (`PREAMBLE_CNTL`, `CONTEXT_CONTROL`, the `vi_cs_data` context
   registers from `clearstate_vi.h`, `PA_SC_RASTER_CONFIG` = 2 for
   Carrizo, `CLEAR_STATE`) and the CE partitions (`SET_BASE`).
4. **The ring.** `CP_RB0_*`, VMID 0, with the write pointer in dwords. The
   GART is off and the MC's system aperture is zero
   ([register record](../decisions/2026-10-28-carrizo-register-probe.md)),
   as they were when SDMA fetched from the carve-out at
   GPU `F4_0F000000`. Whether the CP's fetches pass through VMID 0 the same
   way is the first thing Phase 1 measures.
5. **GCN 3 shaders**, not Evergreen VLIW: scalar and vector ALUs, `EXP`
   exports, VS and PS with user SGPRs for constants. A new encoder from
   AMD's GCN3 ISA reference, host-tested against shaders Mesa's radeonsi
   emits for the same programs.
6. **Tiling.** The BIOS wrote the GB_TILE_MODE tables (`GB_ADDR_CONFIG`
   at Linux's Carrizo golden value). Linear-aligned colour and depth
   buffers first, as Cedar's linear colour buffer ended up.

## Phases

1. **CP bring-up probe** (`CZCP.VXD` + `V9XCZCP.EXE`), the SDMA probe's
   op-list design over the graphics registers. Steps: RLC resume,
   PFP/CE/ME load, ring 0 in a window at BAR0 + 248 MiB (clear of the
   driver's SDMA window at 240-244 MiB), unhalt, Linux's ring test
   (`SET_UCONFIG_REG` writing `DEADBEEF` to `SCRATCH_REG0` over the CPU's
   `CAFEDEAD`), then the start preamble without `vi_cs_data` and a second
   scratch write behind it. CP and RLC halted again after, as found.
2. **The full clear state** (`vi_cs_data`) and `gfx_v8_0_gpu_init`'s
   writes that the BIOS did not do, read back with V9XCZMM.
3. **A solid rectangle**: a hand-assembled GCN 3 VS and PS, linear colour
   buffer, into the probe window, byte-exact.
4. **Gouraud, texture, depth**, the shader-variant scheme as stream
   builders, host-tested. As on Cedar, the draws take the HAL's own
   vertex from the start: the render core's 32-byte TL vertex (sx, sy,
   sz, rhw, D3DCOLOR, specular, tu, tv). The VS fetches it with two FLAT
   loads at base + 32 x VertexID, base in user SGPRs, and exports sx, sy,
   sz and rhw unchanged as the position: the VTE takes screen-space XY,
   Z and 1/W. Steps, each a V9XCZ3D draw checked pixel by pixel:
   1. `/gouraud`: a red ramp across the target from per-vertex
      D3DCOLORs, interpolated by `v_interp` (+-2 per channel);
   2. `/depth`: a Z16 buffer, LESS, three overlapping rectangles;
   3. `/texture` and `/modulate`: an image descriptor and sampler in
      SGPRs and `image_sample`, then texel times diffuse.
   Every shader keeps to GCN 3's one constant-bus read per VALU
   instruction ([first rectangle](../decisions/2026-10-28-carrizo-first-rectangle.md)).
5. **The HAL**: `d3d_vi.c` behind `V9X_D3D_ENGINE_OPS`, then V9XD32,
   V9XDDP's 3D checks and a game. Cedar's split: `engines\vi_cp.c` brings
   the graphics CP up at the first Direct3D batch and leaves it running
   (gpu_init, the RLC and PFP/CE/ME microcode from the tree, the ring, the
   clear state, a ring test), `d3d_vi_map.c` turns the neutral draw into
   `v9x_vi_3d_tl_draw`'s state (pure, host-tested) and `d3d_vi.c`
   resolves surfaces, places them and submits. The CP's ring, shaders and
   vertex arena share SDMA's 4 MiB window at BAR0 + 240 MiB; each engine
   drains the other before it touches what the other wrote. Steps:
   1. **First light**: 16 and 32 bpp targets, flat and Gouraud colour,
      Z16 with every function, RGB565 and ARGB8888 textures, point and
      bilinear, clamp, COPY/DECAL and MODULATE. The colour path keeps
      memory byte order (D3DCOLOR's B, G, R, A as x, y, z, w; the T#
      selects X, Y, Z, W), as Cedar's does, so nothing swizzles.
      Everything else is refused and counted. V9XD32 at both depths.
   2. Blending and the alpha test; ARGB1555 and ARGB4444 textures after a
      probe of GCN's field order; wrap and mirror.
   3. Specular and fog, mip chains, the colour key; then a game.

   Steps 1 to 3 done except the game. Step 1 is
   [first light](../decisions/2026-10-28-carrizo-d3d-hal-first-light.md).
   Steps 2 and 3 are in
   [the fixed-function record](../decisions/2026-10-28-carrizo-d3d-blend-and-shader-fixed-function.md):
   - blending needed the compressed FP16 export;
   - a 565 texture reads W = 1;
   - the key, fog and alpha test are in a PS built per draw;
   - the mip layout was measured.

   V9XD32 passes at 16 and 32 bpp, as do V9XDDP's fog, specular, key,
   4444 and mip checks (boot 28).
   [3DMark 2000](../decisions/2026-10-29-carrizo-3dmark2000.md) runs to
   its score, with correct frames, since textured shaders sample in
   whole-quad mode (boot 33). The engine draws in the PA's guard band.
   Next: V9XDDP's rows that still read 0, and a game.

## Decisions

- **Microcode in the tree**, as for SDMA (cefbf989) and Cedar: the four
  images go through `gen-carrizo-microcode.ps1` under LICENSE.amdgpu once
  Phase 1 has run them. Until then the probe reads them from its folder on
  the guest.
