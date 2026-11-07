# RIVA 128ZX: 3DMark 99 Max drew a third of its draws without their textures' placement

Date: 2026-11-07. Machine: M-7V636C (remote help), RIVA 128ZX
(12D2:0018, 8 MiB), Windows 98 first edition, nvidia package. 3DMark
99 Max at its defaults: 800x600x16, 16-bit Z, triple buffered. HAL
counters are V9XTRACE snapshots taken before and after each run.
Evidence is in `docs/probe/m7v636c-riva128-3dmark99-2026-11-07/`.

This was the first run of 3DMark 99 Max on this HAL. Scores are
recorded, not compared.

## Before: boot 42, HAL `1515d880` (dirty)

The benchmark finished: 165 3DMarks, 3,065 CPU 3DMarks. Counter deltas
over the run:

| Counter | Delta |
|---|---|
| D3dTextureCreates | 15,430 |
| TexturePlaced (by `v9x_d3d_nv3_create_surface`) | 11,729 |
| D3dTextureCreateSysmem | 0 |
| M64Draws | 32,260 |
| M64Refused | 11,837 |
| M64Policy03 (`REFUSE_TEXTURE`) | 11,837 |

So 3,701 video-memory textures existed that the NV3 had not placed. Every
draw that used one was refused at `v9x_d3d_nv3_find_surface`, and with
no fallback for this chip those draws were dropped.

## Why

`v9x_d3d_nv3_create_surface` returns NOTHANDLED when it declines a
texture, and DirectDraw then places the surface itself as linear levels.
For engines that sample linear levels (Mach64, SiS, VSA-100), that
placement still draws. The NV3 samples only its own swizzled copy, which
sits beside the linear levels in the block it allocates, so a texture
DirectDraw placed can never be drawn. Linear levels, the swizzled chain
and, for 4444, the alpha-free copy add up to about twice DirectDraw's
bytes, so under memory pressure DirectDraw still found room where the NV3
had none.

Which of the NV3's declines fired was not counted: no room, a full
record table, size or format. "No room" was the hypothesis, and the
change below tests it: if it were wrong, REFUSE_TEXTURE would have
stayed high.

## Change

When the NV3 would take a texture but has no room in the heap, or no
free record, it now fails the create with `DDERR_OUTOFVIDEOMEMORY`
(`V9X_DDERR_OUTOFVIDEOMEMORY`, `8876017Ch`) and returns HANDLED. The
program is told the card is full, as on any other full card. Declines
for size or format still return NOTHANDLED, unchanged.

## After: boot 43, the same build plus this change

HAL SHA-256 `51B0E512...`, installed by WININIT rename and `reset`,
hash checked. The benchmark finished: 189 3DMarks, 3,160 CPU 3DMarks.

| Counter | Delta |
|---|---|
| D3dTextureCreates | 19,322 |
| TexturePlaced | 19,322 |
| M64Draws | 40,951 |
| M64Refused | 0 |
| EngineIdleTimeouts, EngineFifoTimeouts | 0, 0 |

Every texture created was placed, and nothing was refused. 3DMark creates
more textures than before (19,322 against 15,430), which fits a program
that now sees its creates fail and retries after freeing others.

## Not established

- Whether the screen is right. Agent screenshots are blind during
  full-screen flips, and nobody looked at the monitor.
- Whether any create failed and 3DMark skipped content because of it.
  Failed creates are not counted. A benchmark that finishes with every
  draw drawn suggests not, but that is not a measurement.
- Whether other games handle the out-of-video-memory answer as well. Thief
  and Half-Life ran with nothing refused before this change, so they
  never reached it.
- `FlipStillDrawing` rose by 1,472,903 in the first run against 908 flips
  handled: the program spinning on the flip status. Not examined.
