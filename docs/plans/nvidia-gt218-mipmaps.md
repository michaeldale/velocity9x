# GT218 mipmaps through a tiled texture pool

Status: **done 2026-11-25** ([record](../decisions/2026-11-25-gt218-mipmaps.md)):
T21 passes and V9XD32 is clean on W9QI0DAD. The TIC also needed word 7's
level range, which the plan below did not foresee. Machine: W9QI0DAD,
GeForce 210 GT218 `10DE:0A65`.

## Why

The GT218 path advertises mip filtering (MIPNEAREST and MIPLINEAR) but
samples level 0 of every texture. Its textures are pitch-linear, and an NV50
pitch TIC is 2D_NO_MIPMAP only (Mesa's `nv50_tex.c`). V9XD32 T21 has
failed on this card since at least 0.53.1: all four sizes read level 0.
T21 writes no result line when it fails, so the failure showed only in
`Failures`. Michael chose to sample the mip levels, not to stop
advertising them (2026-11-25).

## Shape

Mipmapped textures keep their linear surfaces for the runtime's Locks
and Blts. A tiled copy of the whole chain lives in a pool, and the
sampler reads that copy, as the cube maps' copies are read
([2026-11-25-gt218-cube-maps.md](../decisions/2026-11-25-gt218-cube-maps.md)).

- **The pool.** 32 MiB directly below the 2 MiB engine reserve, taken off
  the DirectDraw heap by the 16-bit reserve hook. That is Michael's choice
  of size (2026-11-25), and it changes the heap split he set on 2026-11-14.
  - It lives outside the heap because a raw heap block outlives nothing
    across a mode change: DirectDraw rebuilds its heaps and could hand the
    pool to a surface.
  - The HAL finds it as `[fb.vram_bytes, engine.reserve_offset)`, since the
    heap ends where the hook says. It uses only the whole row-block units
    inside it, so the kind-70h pool never shares a row block with the
    linear heap or the channel (`nv50_rblock.h`).
  - A mapping too small for it gets no pool: the heap ends at the reserve
    as before, and textures sample level 0.
- **Allocation.** A first-fit list of blocks on the 512-byte tile within
  the pool, given back when the texture goes. A texture that finds no
  room samples level 0, as today.
- **Layout** (host-tested, Mesa's `nv50_miptree.c`
  `init_layout_tiled` with `nv50_tex_choose_tile_dims`):
  - each level's pitch on the 64-byte GOB;
  - its rows on its own block height, the smallest of 4, 8, 16, 32 and 64
    rows covering twice its height in rows (Mesa doubles ny);
  - levels one after another.
  - The TIC carries level 0's tile mode. The hardware is taken to shrink
    the block height for smaller levels by the same rule; that is what
    T21 tests.
- **Copies.** One 502D copy per level, linear to tiled, each with its
  level's tile mode, before a draw samples a texture created or written
  since (as the cubes do).
- **TIC and TSC.** 2D (TWO_D, 4000h) block-linear, last level in word 5
  bits 31:28. The TSC's mip filter is nearest 80h or linear C0h (from the
  stage's MIPFILTER), and its maximum LOD in word 2 bits 23:12 is
  4.8 fixed point, levels - 1.

## Steps

1. Host-tested layout, TIC and TSC (`nv50_d3d.c`), and the reserve split
   (`gt218_reserve.c`, `test_nv50_init.c`).
2. HAL: the pool allocator, placement of a mipmapped texture's tiled
   chain beside its linear levels, the copies, mapping through the copy.
   V9XD32 T21 writes `T21Result` when it fails.
3. Measure: V9XD32 T21 on W9QI0DAD, the release probes unchanged, the
   heap's new size in V9XBOOT.INI.

**Gate:** T21 passes; nothing else in V9XD32 or the probes changes.

## Not in this plan

Cube maps' levels past 0, and DXT (step 7 of
[nvidia-gt218-ps11.md](nvidia-gt218-ps11.md)).
