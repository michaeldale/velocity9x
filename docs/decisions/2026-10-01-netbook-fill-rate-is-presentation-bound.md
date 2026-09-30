# The netbook's Final Reality fill rate is bound by presentation, not by pixels

Date: 2026-10-01
Machine: MICHAEL-NETBOOK, 945GSE / GMA 950, boot 78, 1024x576x16 desktop,
HAL `d78d4f4`.
Evidence: `../probe/final-reality-full-2026-10-01/netbook-boot78-fill-only-*`

## Why

The full run of the same morning
([record](2026-10-01-final-reality-full-runs-netbook-and-gateway.md)) gave
the GMA 950 a Fill rate of 8.18 Mpixels/s, against 38.26 on a 450 MHz
Mach64. That is far below anything the silicon should be limited by, and
the whole-boot HAL timers could not say which test their time belonged to.

## Run

Final Reality Fill rate only, one pass, with a V9XTRACE snapshot before and
after and nothing else run between them (246 s of uptime, including launch
and navigation). Result: **8.80 Mpixels/s, 1.90 R marks**.

## Measured (post minus pre)

| | |
|---|---|
| Frames flipped (`FlipHandled`) | 143 |
| DrawPrimitives calls / records / triangles | 293 / 287 / 575 |
| Gen3 batches submitted | 144, none refused |
| Colour and depth clears (`TimeBltFill`) | 298 calls, 0.12 s |
| Flip calls | 1,597, of which **1,454 returned `WASSTILLDRAWING`** because the flip window was closed (`FlipWindowClosed`) |
| Draws that waited for a pending flip (`DrawsFlipWaited`) | **142 of 144** |
| Time inside the engine's draw (`TimeEngineDraw`) | 4.36 s, **30.3 ms per batch** |
| of which ring-space waits | 0.51 s |
| of which ring writes and decode | under 0.01 s |
| Time in Flip itself | 0.02 s |

So each frame is one batch of about four triangles, and it looks like
this: FR calls Flip about eleven times before the HAL's flip window opens
and one is accepted; the frame's single draw then spends about 30 ms in
the engine, nearly all of it outside every timed sub-phase. The one
untimed wait on that path is `v9x_flip_wait_done` (d3d_i9xx.c, the
pending-flip wait at the top of `v9x_d3d_i9xx_draw_triangles_body`), and
142 of the 144 draws took it. Ring-space waits, which is what a busy GPU
would show, account for 0.5 s of the 4.4.

The test is therefore paced by two waits on the display per frame, one in
Flip and one in the next draw, not by how fast Gen3 fills pixels.

## Inferred, not measured

- **The frame rate.** If Final Reality's pixel count per frame is its
  four triangles covering the 640x480 target twice (614,400 pixels), 8.80
  Mpixels/s is about 14 frames a second, 70 ms a frame, of which the HAL
  accounts for about 30. The per-frame pixel count is an assumption about
  the test; what the rest of each frame is spent on, outside the HAL, was
  not measured.
- **That the flip wait is the 30 ms.** It is the only untimed wait on the
  path and nearly every draw took it, but it has no timer of its own. A
  `V9X_TIME_*` phase around `v9x_flip_wait_done` would turn this into a
  measurement.
- **Why the Mach64 is not paced the same way.** Its flip and draw paths
  were not examined here.

## What this rules out, for this test

Uncached-aperture CPU writes, CPU clears and slow GPU pixel throughput are
not what bounds it: clears total 0.12 s, the Lock path is microseconds,
and the GPU-busy signal (ring-space waits) is small.
