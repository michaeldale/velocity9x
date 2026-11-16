# Oland: the first 3D draw, a solid rectangle exact on the first run

Date: 2026-11-16

Status: **measured, one run.** W9QI0DAD (Oland XT `1002:6610`), boot 49,
ati Oland row with DirectDraw on SI's DMA engine, desktop 1024x768x16.
Step 2 of [`../plans/ati-oland-gfx6-3d.md`](../plans/ati-oland-gfx6-3d.md),
the first draw. Evidence:
[`../probe/oland-w9qi0dad-2026-11-16/sicp-rect1/`](../probe/oland-w9qi0dad-2026-11-16/sicp-rect1/).

## What ran

`V9XSICP /rect`, built at `11bd02b1-dirty` (this commit's tree). After the
probe's step-1 and step-2 bring-up (gpu_init list, RLC, CP microcode,
ME_INITIALIZE, ring test):

1. **Start of stream.** `v9x_si_3d_start`, the second half of
   `gfx_v6_0_cp_gfx_start`: PREAMBLE_CNTL begin, SI's clear state (882
   dwords in seven context runs, `si_clear_state.c`, generated from
   `clearstate_si.h` by `gen-oland-clear-state.ps1`), PREAMBLE_CNTL end,
   CLEAR_STATE, and the VGT reuse/dealloc pair. No CONTEXT_CONTROL and no
   raster configuration, as in Linux's SI start.
2. **The draw.** Carrizo's builder, `v9x_vi_3d_solid_rect`, in a new GFX6
   mode (`struct v9x_vi_pm4`'s `gfx6`). Every context register it writes
   has the same index and name in `gfx_6_0_d.h`, checked one by one.
   GFX6 mode changes three things:
   - each stage's SH run starts at `SPI_SHADER_PGM_LO` (SI has no RSRC3
     and no LATE_ALLOC);
   - `VGT_PRIMITIVE_TYPE` goes through SET_CONFIG_REG at 2256h (SI has no
     SET_UCONFIG_REG);
   - the fence is SI's: `CP_COHER_CNTL2` 0, a SURFACE_SYNC without TC_WB
     (28C00000h), then EVENT_WRITE_EOP with the event alone (514h).

   The target is 64x16 32 bpp, linear-aligned (tile index 8,
   COLOR_8_8_8_8), and the rectangle runs from (8,4) to (40,12), all
   Mullins' values.
3. **The shaders.** `vi_shader.c`'s solid VS (15 dwords) and PS (7),
   rewritten by `v9x_si_shader_from_vi`. That is Mullins' GCN 3 to GCN 2
   translator with FLAT refused: every encoding it emits is LLVM's shared
   `gfx6_gfx7` one, and FLAT is the one format SI lacks. The probe-marker
   shaders store through FLAT, so this run has no VS/PS markers.

## Result

| | |
|---|---|
| Fence | **3D3D3D3D after 8 polls** (`FenceLanded=PASS`), read pointer 700h = the write pointer |
| Pixels | **`Drew=PASS`**: all 256 inside `BF4080FF`, 0 inside different, 0 outside changed |
| Channel order | R 1.0 in the low byte, then G 0.5 (80h), B 0.25 (40h), A 0.75 (BFh): the same pixel as Carrizo and Mullins |
| Ring test, preamble | PASS |
| gpu_init read-back | as in 2026-11-16-oland-gpu-init.md (the same two partly-reading registers) |

Afterwards, same boot: V9XBLTX `/random:50` byte-exact, 103 cases, 53 on
the DMA engine, no timeouts (`bltx-after-rect-16bpp/`); the desktop
intact (`sicp-rect1/desktop-after.png`).

## What this settles

- SI's clear state, the shader core (VS and PS waves launched with the
  BIOS's CU enables and gpu_init's), the rasteriser, the CB writing a
  linear-aligned target, and SI's end-of-pipe fence all work as built.
- The GCN 2 words for VOP1/VOP2/VOPC, EXP and s_endpgm run on GCN 1.
  The textured and Direct3D shaders also use VINTRP, VOP3,
  image_sample and the SOP1 moves; those are not run yet.

## Not yet

- **Gouraud, textures, depth.** Mullins' other first draws all load
  vertices through the TL vertex shader, which uses FLAT. SI needs that
  fetch rewritten: MUBUF `buffer_load` from a buffer resource, or SMRD
  scalar loads, as SI-era drivers do. That is the next piece.
- Tiled targets: nothing here is tiled, so `GB_ADDR_CONFIG`'s partial
  read-back is still open.
