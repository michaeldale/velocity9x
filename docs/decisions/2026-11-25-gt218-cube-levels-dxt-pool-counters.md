# GeForce 210 cubes: a whole chain per layer, DXT cubes, and pool counters; TIC word 5 must name the resource's levels; Car Chase's ground whitens on DXTC

Date: 2026-11-25. Machine: W9QI0DAD (AMD FX-6300, Windows 98 SE), ASUS
GeForce 210, GT218 `10DE:0A65`, desktop 1024x768x16, boots 156-158.
Follows [the DXT record](2026-11-25-gt218-dxt.md). Evidence:
[`../probe/gt218-cube-levels-dxt-w9qi0dad-2026-11-25/`](../probe/gt218-cube-levels-dxt-w9qi0dad-2026-11-25/).

## Step 8 of the plan: Nature's frame

[nvidia-gt218-ps11.md](../plans/nvidia-gt218-ps11.md) step 8 asks for
Nature's Image Quality frame. This machine's 3DMark 2001 SE is not the
Pro edition and has no Image Quality test. The HDMI frames OBS took of
Nature during the DXT run stand in for it
([DXT record](2026-11-25-gt218-dxt.md), `obs/`): mountains, the distant
tree line, the rock face close up, trees, grass, the cottage. The
vsync-on run before DXT shows the lake dark and reflecting
([3DMark record](2026-11-25-gt218-3dmark-after-step6-mipmaps.md),
`obs-vsync-on/`). **Not done:** a frame compared pixel for pixel
against a reference.

## What changed

- **Cube layout.** A cube's tiled copy used to hold level 0 alone, in
  tile mode 10h, one face to a 512-byte-aligned layer. It now holds
  Mesa's array layout:
  - each layer is a face's whole chain, laid out as a 2D chain
    (`v9x_nv50_d3d_mip_layout`, tile mode per level by
    nv50_tex_choose_tile_dims);
  - the layer stride is that chain rounded up to level 0's tile;
  - the copy brings every face's every level up to date.
- **Sampling.** The cube TIC now reads between levels when the stage's
  min filter is a mip filter, through `v9x_nv50_d3d_tic_levels`, which
  the 2D mip TIC now shares.
- **DXT cubes.** These are placed the same way, counted in 4 x 4 blocks,
  where before they were left to DirectDraw and their draws refused.
  Their faces Lock through the packed copy as any DXT level.
- **Pool counters.** Four counters appended to `V9X_D3D_DIAGNOSTICS`
  (within ABI 2026112001), printed by V9XTRACE:
  - `Nv50PoolSize`, the pool's usable bytes;
  - `Nv50PoolUsed`, the bytes held now;
  - `Nv50PoolPeak`, the most held since the pool was laid out;
  - `Nv50PoolRefused`, textures that got no copy (no room, or no free
    record).

## TIC word 5: one failure, its cause measured

The first boot with the new layout (156) failed V9XD32 T19's Cube row:
911 mismatches, worst channel 239
([`release-boot156-tic5-zero/`](../probe/gt218-cube-levels-dxt-w9qi0dad-2026-11-25/release-boot156-tic5-zero/)).
T19's cube has two levels and samples level 0 with no mip filter.

The TIC described level 0 alone: word 5's last level was 0. The copy,
however, laid each 8 x 8 face's two levels in a 1,024-byte layer. Mesa's
nv50_tex.c writes the resource's last level in word 5 and the view's
range in word 7, and the sampler steps a cube's layers by the chain that
word 5 describes. So the faces were read 512 bytes apart from where they
were written.

Fix: word 5 now always names the resource's last level, while word 7
and the TSC open past level 0 only for a mip filter. Boot 157: T19 Cube
passes (0 mismatches, worst channel 8), and V9XD32 is `Result=PASS`
with 6 of 6 probe runs exiting 0.

Hypothesis killed: that the old fixed mode 10h was needed for cubes. At
8 x 8 the new layout's level 0 is mode 10h anyway, and the failure
followed word 5, not the tile mode.

## Gates

`build-host.ps1` and `run-checks.ps1` passed. New host tests: the cube
layout (one level at 64, 4 and 20 texels, a chain of seven, a DXT1 cube,
refusals), the level helper (resource levels without a mip filter, then
with one), and the map's cube paths (level 0 view, mip view, a DXT cube
without a pool record).

## On the card

- **Boot 157 release probes:** pool size 33,423,360 bytes, peak 12,800,
  none refused.
- **3DMark 2001 SE**, `VSync=1` for the run and `VSync=2` put back after
  (hash checked):
  - Score 2502 in 7.3 min, recorded, not compared.
  - No refusal (`M64Policy10` 0), and the usual 160 guard-band drops.
  - Pool peak 19,998,208 bytes of 33,423,360 (60 %), none refused, back
    to 0 used afterwards. So 3DMark's DXTC set fits with 13 MB spare.
  - A first attempt was cut off at 2.3 min by the run script, not by
    the driver. Its `V9XTRACE.EXE` in `C:\V9XNVP` was a build from
    before the struct grew; it refused the larger snapshot
    (`Error=abi-mismatch`), and the script took the missing flip count
    for a freeze. The release checks install eight files and not
    V9XTRACE, so `SYSTEM\V9XTRACE.EXE` was also still 0.59.0's from the
    B13 rollback; both were replaced
    ([`3dmark2001se-aborted-stale-v9xtrace/`](../probe/gt218-cube-levels-dxt-w9qi0dad-2026-11-25/3dmark2001se-aborted-stale-v9xtrace/)).
- **Car Chase High:** the ground by the robots draws near-white from
  about 18 s on the test clock. It began with DXT, not with this change;
  the DXT run's pale patch was the same thing. Filed:
  [issue](../issues/2026-11-25-gt218-car-chase-ground-white-on-dxtc.md).
- **Boot 158, a new T19 row, `Dxt5`:** 8 x 8, eight-value alpha, every
  index used. It passes first time (0 mismatches, worst channel 9 of 9),
  so DXT5 decoding is not the cause of the white ground. The row is in
  V9XD32 for every chip; the others have not run it yet. V9XD32 is
  `Result=PASS` with Cube, Dxt1, Dxt3 and Dxt5 passing
  ([`release-dxt5/`](../probe/gt218-cube-levels-dxt-w9qi0dad-2026-11-25/release-dxt5/)).

## Not shown

- **Mip-filtered and DXT cubes on the card.** T19's cube samples level 0
  only, and nothing run here makes a DXT cube. UT2004, whose DXT5 cubes
  the HD 4600 needed, cannot run on this card: it needs a 24-bit Z,
  which the GT218 path does not offer.
- **Pixel-exact Nature.** See step 8 above.
- **What whitens Car Chase's ground** (the issue above).
