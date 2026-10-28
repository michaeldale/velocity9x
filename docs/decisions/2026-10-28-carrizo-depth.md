# Carrizo depth-tests into a 1D-tiled Z16 buffer: LESS rejects and accepts as it should, and the buffer holds the exact values

Date: 2026-10-28

Status: **measured, one machine, two runs; passes.** M-XP7053's Radeon R7E
(`1002:9874`), the same session as [Gouraud](2026-10-28-carrizo-gouraud.md).
Step 2 of Phase 4 of [the 3D plan](../plans/ati-carrizo-gfx8-3d.md). The
reports are in
[`../probe/m-xp7053-carrizo-depth-2026-10-28/`](../probe/m-xp7053-carrizo-depth-2026-10-28/).

## What was built

`v9x_vi_3d_tl_draw` can now take a depth buffer: a Z16 surface the size of
the target, a compare function (`V9X_VI_ZFUNC_*`, D3DCMPFUNC less one) and
a write flag. The depth state comes after the shared no-depth state, so
its writes are the ones that hold. The host test checks that, and it was
written first: it failed to build until the fields existed.

The registers, from `gfx_8_0_sh_mask.h` and `_enum.h`:
- **Surface.** `DB_DEPTH_INFO` through `DB_DEPTH_SLICE` in one run:
  - `DB_DEPTH_INFO` = `21h`: ADDR5_SWIZZLE_MASK, as radeonsi sets it,
    and ARRAY_MODE 1D_TILED_THIN1;
  - `DB_Z_INFO` = `0050A001h`: Z_16, with tile split 5 and tile index 5
    taken from Carrizo's `GB_TILE_MODE5`, which Linux's table makes 1D
    thin with depth micro-tiling;
  - stencil invalid; read and write bases on the buffer;
  - `DB_DEPTH_SIZE` = `807h` and slice 15, for 64x16.
- **No HTILE.**
  - `DB_HTILE_DATA_BASE` = 0;
  - HiZ and HiS forced off (`DB_RENDER_OVERRIDE` = `2Ah`);
  - depth and stencil compression disabled (`DB_RENDER_CONTROL` = `60h`).

  These are Cedar's choices; the bits are the same on GFX8.
- **`DB_DEPTH_CONTROL`**: Z enable, Z write, ZFUNC.

`V9XCZ3D /depth` runs Cedar's test:
- a Z16 buffer at window offset D000h, filled with FFFFh (1.0);
- one draw of three rectangles, LESS with writes:
  - red over the whole target at z 0.5;
  - green over the left half at 0.75, behind it;
  - blue over the right half at 0.25, in front.

## What ran

| Run | Draw | Result |
|---|---|---|
| 1 | `/depth` | colour and Z buffer as predicted |
| 2 | `/gouraud`, regression | all 1,024 pixels within +-2 |
| 3 | `/depth` again | the same as run 1 |

- **Colour.** All 512 pixels left of x 32 are red (`FF0000FF`) and all
  512 right of it are blue (`FFFF0000`). No green pixel anywhere.
- **Z buffer.** The 16 tiles, 8x8 samples and 128 bytes each, lie in two
  rows of eight. In each row, tiles 0-3 hold `8000h` in every sample and
  tiles 4-7 hold `4000h`. Those values are 0.5 and 0.25 as 16-bit unorm.
- **The fence** landed in both runs.

## What the evidence says

- **The test rejects and accepts.** The green rectangle at 0.75 lost to
  the red at 0.5 it was drawn after, so its fragments were rejected. The
  blue at 0.25 replaced red, so its fragments passed. Both written values
  are exact.
- **The layout matches the 1D tiled description.** Each tile is
  contiguous, the tiles run in rows, eight across for a 64-pixel pitch.
  Every region boundary falls on a tile edge, so the order of samples
  inside a tile is not tested.
- **The DB needs no HTILE here**, as on Cedar.

## Not checked

- The other compare functions, and a draw with writes off.
- A depth boundary that is not tile-aligned, which would test the sample
  order inside a tile.
- 2D-tiled depth (tile modes 0-4), Z24/Z32, stencil, and depth clears by
  the DB itself (the buffer was cleared through BAR0).
- A depth surface whose pitch differs from the target's: the builder
  ties them together.
