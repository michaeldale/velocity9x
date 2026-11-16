# Direct3D on AMD Oland (GCN 1, GFX6)

Date: 2026-11-16

Status: Step 1 done on W9QI0DAD (the CP runs and passes Linux's ring
test). Step 2's first half done: Oland's gpu_init list runs and reads
back, except `GB_ADDR_CONFIG` and `DMA0_TILING_CONFIG`, which read back
partly
([`2026-11-16-oland-gpu-init.md`](../decisions/2026-11-16-oland-gpu-init.md)).
The first draw is done: SI's clear state, vi_3d.c's builder in a GFX6
mode and the CI shader words (FLAT refused) drew a solid rectangle
exactly on the first run
([`2026-11-16-oland-first-rectangle.md`](../decisions/2026-11-16-oland-first-rectangle.md)).
The TL vertex shader now fetches through MUBUF and a V# instead of FLAT,
and the Gouraud ramp is exact
([`2026-11-16-oland-gouraud-mubuf-fetch.md`](../decisions/2026-11-16-oland-gouraud-mubuf-fetch.md)).
Textures, modulate and Z16 depth (GFX6's tile-index-only DB setup, Oland
1D depth index 4) pass on the first run
([`2026-11-16-oland-texture-depth.md`](../decisions/2026-11-16-oland-texture-depth.md)):
step 2 is done, and with it step 3 for every instruction these programs
use. Step 4 done: the HAL brings the CP up in an Oland mode and V9XD32
passes at 16 and 32 bpp
([`2026-11-16-oland-direct3d-hal.md`](../decisions/2026-11-16-oland-direct3d-hal.md)).
Step 5 started: 3DMark 2000 (3808) and 2001 SE (1486) run to a score,
rendering correctly, with no timeouts or new refusals
([`2026-11-16-oland-3dmark.md`](../decisions/2026-11-16-oland-3dmark.md)).
V9XGLP and V9XGLIDP pass, and Quake 3 (72.9 fps) and Quake 2 draw every
batch on the chip, windowed at 16 bpp
([`2026-11-16-oland-opengl-glide-quake.md`](../decisions/2026-11-16-oland-opengl-glide-quake.md)).
Step 5 is done except fullscreen and 32 bpp GL runs and a Glide game.

Phase 6 of [`ati-oland-gfx6.md`](ati-oland-gfx6.md): the graphics
command processor and Direct3D on W9QI0DAD's Oland XT (`1002:6610`,
2 GiB, 6 CUs). Phases 0-5 are recorded in
[`2026-11-16-oland-first-survey.md`](../decisions/2026-11-16-oland-first-survey.md):
DirectDraw runs on SI's DMA engine, byte-exact, and the chip takes flips.

The route is the one Mullins (GCN 2) took two weeks earlier, from a
standalone probe to the HAL. That plan and its records are the model:
[`ati-mullins-gfx7.md`](ati-mullins-gfx7.md) Phase 6 and the
`2026-11-08-mullins-*` decisions.

## Sources

`build\external\carrizo\linux\` (not in git): amdgpu `gfx_v6_0.c`, `si.c`,
`sid.h`, `clearstate_si.h`, `si_enums.h`, the `gca/gfx_6_0` and
`oss_1_0` headers; radeon `si.c`, `sid.h` (byte offsets). Microcode:
`build\external\oland\firmware\oland_{pfp,ce,me,rlc}.bin` (linux-firmware,
`LICENSE.amdgpu`). Still to fetch: AMD's *Southern Islands Series
Instruction Set Architecture* reference, for step 3.

## Steps

1. **CP ring test (done 2026-11-16).** `V9XSICP` (`tools\diag\si_cp*.`):
   RLC microcode through SI's indexed port, PFP/CE/ME through theirs,
   ring 0, ME_INITIALIZE, Linux's scratch-register ring test, and the
   clear-state preamble, with everything put back halted. Both PASS on
   the first run;
   [`2026-11-16-oland-cp-ring-test.md`](../decisions/2026-11-16-oland-cp-ring-test.md).
2. **gpu_init and the first draws.** `gfx_v6_0_constants_init` for
   Oland: golden registers (`si.c`'s `oland_golden_*`), the tile-mode
   table (`gfx_v6_0_tiling_mode_table_init`, Oland's case),
   `GB_ADDR_CONFIG` (which reads 0 after the BIOS), and `setup_rb` and
   raster config for its 2 RBs, as a generated table like
   `cik_gpu_init_tables.c`. Then `si_cs_data` (clear state) and a
   `V9XSI3D` probe on `V9XCK3D`'s pattern: a solid rectangle, a Gouraud
   triangle, a textured quad, Z16, each read back exactly.
3. **The GCN 1 shaders.** `cik_shader.c` emits GCN 2. SI's encodings are
   mostly the same, but SMRD offsets, some VOP3 opcodes and the export
   and buffer instructions must be checked one by one against the SI ISA
   guide. A host test pins each instruction word, as Mullins' did.
4. **Direct3D HAL.** `vi_cp.c` and `d3d_vi.c` in an Oland mode behind
   the row's identity bit, with `V9X_DD_ENGINE_CAP_D3D` stamped once a
   boot passes V9XD32 at 16 and 32 bpp.
5. **Programs.** 3DMark 2000 and 2001 SE, Quake 2 and 3 through the ICD,
   and the Glide probes, as on Mullins.

## Unknowns that need a measurement

- Whether the RLC needs anything more once the CP runs real draws
  (save/restore list, clear-state buffer). Oland has no power or clock
  gating flags in amdgpu, so Linux gives it neither; step 1 ran without.
- The BIOS leaves coarse-grain clock gating on (`RLC_CGCG_CGLS_CTRL` 3).
  gfx_v6_0 does not touch it and step 1 ran with it on. Whether draws
  care is step 2's to find.
- Engine clocks: the card runs at whatever the BIOS set. Scores are
  recorded, not chased.
