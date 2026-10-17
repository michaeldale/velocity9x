# Haswell, 3DMark 99: every flip is declined, so one frame in three is drawn on screen; Game 2 and the large-texture tests are slow

Date: 2026-10-17. Status: open. Measured on the i5-4570 (`8086:0412`),
boot 15, HAL `4e562dc3-dirty` plus F2b (`bf173a5b`, V9XHAL.DLL by
WININIT). Evidence: `docs/probe/hsw-4570-3dmark99-2026-10-17/`.

## What was seen

Michael's own Quick Run showed "lots of flickering, different to previous
issues", and very bad Game 2 performance. After a power-off, the full
default benchmark was driven over the agent: 800x600x16, 16-bit Z, triple
buffer, VSync off, every test.

| | |
|---|---|
| 3DMarks / CPU 3DMarks | 3059 / 121455 (recorded, not compared) |
| Game 1 / Game 2 | 56.9 / 20.9 fps |
| texture rendering 2 / 4 / 8 / 16 / 32 MB | 580.0 / 389.6 / 230.4 / 7.3 / 3.6 fps |
| fill rate, single / multi | 206.4 / 207.3 Mtexels/s |
| bump mapping, anisotropic | not supported |

A GDI screenshot of Game 1 (`game1-frame.png`) shows buildings and cars
textured, with the ground and sky flat grey. Screenshots show the page at
offset 0, which is not always the frame the game finished, so the grey
is not yet established as a rendering defect.

## What the driver counted

From `V9XSNA5-before.INI` to `V9XSNA6-after.INI`, 3DMARK.EXE the only
Direct3D process:

| | |
|---|---|
| Flip calls / `FlipHandled` / `FlipDeclined` | 77,714 / 0 / 77,714 |
| `DrawsIntoPresented` | 300,727 |
| `R3dListSinkBatches` (engine batches) | 914,221 |
| `R3dListTrianglesIn` | 25,111,540 |
| texture creates = destroys | 17,165 |
| CreateSurface / Lock calls | 28,498 / 52,885 |
| Z placed (pitch 1664, 4 KB base) / depth accepted | 3 / 3 |
| refusals (`M64PolicyLast`, every `D3dTextureRefused*`) | 0 |
| engine idle timeouts | 0 |
| `VbeVramBytes` | 33,488,896 |
| timing buckets | all zero: not enabled in this build |

## Readings

**The flicker is the declined flip.** `hd4600-hsw` publishes no
`ENGINE_CAP_FLIP` (`EngineCaps=0x2013`). `v9x_flip_body` therefore
declines every flip (`v9x_can_set_display_start`), and DirectDraw
presents by rotating the chain's three surface addresses while the
scanout stays at offset 0. Each frame, the buffer at offset 0 is the one
being drawn into: `DrawsIntoPresented` is a third of the batches, as a
three-buffer rotation predicts. So one frame in three is drawn live on
screen, and the other two are never shown.

The netbook did not flicker this way because Gen3 has a scanout backend
(`i9xx_scanout.c`); Haswell has none.

**The 16 and 32 MB texture tests overflow video memory.** The heap is
under 32 MB, and this engine refuses system-memory textures. DirectDraw's
texture management created and destroyed 17,165 textures, with every
upload a Lock and a CPU copy through the aperture. 2-8 MB fit, and they
ran at 230-580 fps.

**Game 2 is not explained yet.** Two candidates, neither measured:

- the same texture churn, if Game 2's set overflows the heap;
- the engine's per-batch cost. Every batch is synchronous: wait for the
  blitter, write the state, rebuild the kernel, submit, then spin on the
  sequence store. At about 27 triangles a batch, 914,221 batches cost
  time whatever the GPU's speed.

The timing buckets were off, so this run cannot separate the two.

## Next

1. **A Haswell scanout backend** so flips are real. The design is to be
   written in `docs/plans/intel-haswell-direct3d.md` before any register
   write, and its claims are to be measured:
   - a plane-A flip writes the DirectDraw offset as a 4 KB-aligned
     `DSPASURF` plus a `(y << 16) | x` `DSPAOFFSET`. i915 programs
     Haswell's linear and tiled primaries this way. The back buffers here
     are not page-aligned (`D3dTargetOffset=0x001D4C00`);
   - the `DSPASURF` write arms the double-buffered update at the next
     vblank, and a flip is done when `DSPASURFLIVE` reads the new base;
   - vblank comes from `PIPEADSL` against `VTOTAL_A`.
2. **Game 2 with the timing buckets on**, to separate texture churn from
   submission cost.
3. Whether Game 1's grey ground and sky are real, once the screen shows
   finished frames.
