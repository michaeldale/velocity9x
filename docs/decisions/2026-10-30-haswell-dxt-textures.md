# DXT1, DXT3 and DXT5 textures on the HD 4600

Date: 2026-10-30. Follows step 6 of
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md).
Machine: the Dell i5-4570 (HD 4600), boots 156-157, HAL-only swaps.
Evidence: [`../probe/hsw-4570-dxt-2026-10-30/`](../probe/hsw-4570-dxt-2026-10-30/).

## Why

A measurement build that published DXT (boot 154) had 3DMark 2001 SE
create 24 DXT1/DXT3 textures for Nature, and was taken as the cause of
Nature's white sky. **That conclusion was wrong** - see the end.

## What changed

- **Published** at DDI 8 where the engine says it places and samples
  them (`dxt_textures`, appended to the engine limits): DXT1, DXT3, DXT5,
  not as cube formats. DXT2 and DXT4 (premultiplied) are not published.
- **Sampled** as BC1_UNORM, BC2_UNORM and BC3_UNORM (Vol 2a, 186h-188h),
  by the fixed-function and PS 1.1 kernels alike
  (`v9x_d3d_hsw_bc_format`, host-tested first against a stub).
- **Placed** by the engine in a block tree. Vol 2d: the alignment fields
  do not apply to compressed formats, whose i and j are the 4 x 4 block.
  `v9x_intel_hsw_miptree_bc` runs Mesa's 2D rule at 4 and 4 and counts
  pitch, offsets and rows in blocks; the uncompressed tree is now the same
  helper at 4, 2 and 1, its tests unchanged. Host-tested first.
- **The CPU's view.** DirectDraw keeps a DXT surface's lPitch as its
  linear size (the same union field), and the runtime writes a level as
  rows of blocks back to back from the address Lock hands out. At boot
  156 the 8 x 8 DXT1 in the tree at pitch 64 had its second row of blocks
  in the first row's padding: T19 Dxt1 254 of 1,040 pixels wrong, black.
  The engine's lock hook now points an engine-placed DXT level, for the
  lock, at a linear copy in the locking process (filled from the tree),
  and Unlock writes it back at the tree's pitch. A DXT texture DirectDraw
  placed (heap named) is refused rather than read wrongly.
- **TEXBLT** copies in cells, a DXT block or a pixel; a DXT surface has
  no bit count, so it copied nothing before. Not exercised: no TEXBLT
  reaches this driver from the DX8 runtime (boot 130).

## Measured (boot 157)

- **V9XD32 T19 Dxt1** (`tex t0; mov r0, t0`, 8 x 8, four colours a block)
  1,040 pixels, error 0; **Dxt3** (`mul r0, t0, t0.a`) max error 8 against
  tolerance 9, at R5G6B5. T12-T16 and the other eight T19 programs pass.
- **3DMark 2001 SE** Nature, Pixel Shader and Advanced Pixel Shader:
  texture refusals 0, shader triangles dropped 0, mip trees declined 0
  (Nature alone without DXT, boot 148: 2,134), no fault. Nature 17.0
  fps, Pixel Shader 59.1, Advanced 27.3 - recorded, not compared.
- Gates: build-host.ps1 and run-checks.ps1 pass.

## The sky: still white, and the boot 154 reading was wrong

With DXT working, Nature's Image Quality frame differs from boot 146's in
149 pixels; the sky and mountains are as white as before, and the frame
still has 67 untextured batches. At boot 154 the whole frame came out
black and the white batches were gone; that was read as "3DMark binds
textures to them once DXT is published". A black frame draws nothing, so
it said nothing about binding. What 3DMark binds those batches to, and
why it is nothing, is open again.

## Not checked

- 32 bpp; DXT5 (published, not drawn by any test); DXT mip chains in
  V9XD32 (3DMark's are chains and draw without refusals, but no frame
  was judged for them); a DXT texture locked by two processes at once.
