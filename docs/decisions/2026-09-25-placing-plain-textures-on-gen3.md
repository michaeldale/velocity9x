# Placing plain Gen3 textures at their own pitch: no bound refusals, 585 to 643

2026-09-25, MICHAEL-NETBOOK, boot 20 (rebooted once before the deploy at
the operator's request, then again to apply it), shared ABI 2026092505.
3DMark 99 Max, 1024x576x16 at the 2112 stride, triple buffering.

## Why

DirectDraw's heap gave every texture a 4 KiB-aligned pitch here
(`MipGapActual=0x1000`), so a 64-texel texture of 128 bytes a row held
256 KB. The heap is 5,660 KB at 1024x576 (8 MiB stolen, less 320 KB the
BIOS keeps, less this driver's 1 MB ring and sandbox reserve, less the
screen), and the triple-buffered chain and Z buffer take about 4.6 MB of
it. The previous run declined 4,408 mip chains for want of room and
refused 65,487 texture binds, each drawn untextured.

## What changed

A lone video-memory texture the bind can sample - 16 bits, square, a power
of two from 8 to 256 - is placed by the Gen3 engine in a block of its own at
the one-level tree layout's pitch (64-byte aligned), page aligned through
the shared `v9x_d3d_i9xx_place_block`, and freed by the same destroy path as
the trees and the padded Z buffer. Two appended counters,
`TexturePlaced`/`TexturePlacedBytes`. Anything else is left to DirectDraw.

## Measured (`...-3dmark1024-report.txt`, `...-3dmark1024-V9XTRACE.ini`)

- **643 3DMarks** against 585 on the same stride without it.
- 35,370 textures placed; **0 binds refused on bounds** (65,487 on the
  previous run at the same point); no texture fell to system memory.
- **No leak**: 8,973 trees + 2 Z buffers + 35,370 textures = 44,345 blocks
  allocated and 44,345 freed; texture creates and destroys 40,303 each;
  three contexts created and destroyed.
- The operator: everything apart from game 2 looked right; game 2 had
  issues, not yet described.

## Not settled

- `MipTreeDeclined` rose to 11,456 (all reason 4, the heap had no
  contiguous room), against 4,408 before. The page of slack each placed
  block carries may be fragmenting the heap. With bound refusals at zero,
  those declined chains are not being drawn from misplaced memory; how the
  application copes with them (retry, evict, smaller texture) is not
  known.
- Whether repeated benchmark runs in one boot degrade - the operator's
  question, prompted by blank later tests on an earlier boot. Within this
  run every placed block was freed; DirectDraw's own heap fragmentation
  across runs is not measured. Two runs back to back in one boot, compared
  counter for counter, would answer it.
- What went wrong in game 2.
