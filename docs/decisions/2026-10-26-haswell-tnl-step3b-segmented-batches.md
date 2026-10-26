# HD 4600: T&L passes the CPU path once a batch carries several state segments

Date: 2026-10-26. Machine: the i5-4570 (W9QI5A83, HD 4600), boot 87,
800x600x16 desktop (565). Step 3b of
[`../plans/hardware-tnl.md`](../plans/hardware-tnl.md), following
[`2026-10-26-haswell-tnl-step3-vertex-stage.md`](2026-10-26-haswell-tnl-step3-vertex-stage.md).
Evidence:
[`../probe/hsw-4570-tnl-step3b-2026-10-26/`](../probe/hsw-4570-tnl-step3b-2026-10-26/).

## Why segments, not the three pieces the plan listed

Step 3 left T&L at par because it drew more batches. A Quake 3 trace of the
step 3 build put the cost per batch at 96.6 µs of EngineDraw with T&L and
103 µs without. Of that, 36.7 µs was RenderDrain and 35.2 µs was RingWrite.
The time is in each batch's fixed cost, not in the vertices, so the lever
is fewer batches.

T&L batches broke on texture or fragment state: 40,655 times in the
indexed Half-Life run. The CPU path cannot merge across those breaks
either. What the vertex stage allows is different: one batch whose
constant block, kernel and vertex buffer are shared, with only the
per-state pointers changing between draws.

The plan's three pieces (array capture, a per-batch visibility test,
dropping the CPU twin) are still open. They were not needed to pass the
CPU path.

## What changed

- **Render interface (ABI 7, unpublished):** `V9X_R3D_TNL` gains an
  ordered list of up to 8 segments. Each segment has its own draw state
  (texture, blend, depth test), its own constants, and a range of the
  shared index list. `v9x_r3d_validate_tnl_segments` requires the ranges
  to partition the indices in order from 0. The HAL refuses segments that
  change the target, the depth buffer or the scissor, or that use two
  texture units.
- **HAL:** `v9x_intel_hsw_draw_workload_vs` emits, per segment:
  - the state pointers;
  - PS;
  - CONSTANT_VS with its dummy primitive;
  - an indexed 3DPRIMITIVE over that segment's range.

  Segment 0's state is laid out as before. Later segments put their
  constants and binding tables in the slot block, and the rest of their
  state in the last 4 KB of the vertex area. The vertices are copied
  once.
- **ICD:** a held T&L batch (`V9X_GL_TNL_HOLD`, on the heap per context)
  merges the next capture when the frame, key, targets, depth enable and
  scissor match:
  - same texture and fragment state: it extends the current segment;
  - different: it starts a new segment;
  - otherwise it is flushed.

  If the engine refuses, each segment falls back to its CPU twin.

## The bug found first

The first segmented build ran Quake 3 at 29.8 fps and drew no T&L batches.
The sink refused any texture whose storage was CPU. The describe call
always reports CPU storage, because hardware residency is decided at
flush. So every capture was refused and flushed. Removing the check, and
copying the texture levels into each segment, fixed it.

## Correctness

The V9XGLP A/B scene now draws six quads with alternating textures, so
segments are exercised:

- **The ICD's counters:** 6 new segments and 7 joins; 120 T&L batches;
  0 refused; 0 failed.
- **Frame comparison against step 3:** the same. 1,800 pixels differ by
  more than 3/255, 112 by more than 8/255 and none by more than 16/255.
  The worst channel is 9/255, and nothing is drawn outside the viewport.
- **Pixels drawn:** 28,196, up from 26,578. The extra pixels are the six
  quads.
- **Overall:** `Result=PASS`. `TnlResult=differ` is the label for "within
  the stated 565 rounding", as in step 3.

## Speed

The same build was run with T&L on and with `V9XGL_TNL=0`.

| Run | T&L on | Off |
|---|---|---|
| Half-Life `timedemo mwd5`, windowed 640x480 | 88.0 | 85.2 |
| Quake 3 demo `timedemo demo001` | 58.6 | 57.1 |

Half-Life T&L counters: 4,048 batches, holding 20,209 new segments and
124,417 joins; 2,427 flushes because all 8 segments were full.

The Quake 3 V9XTRACE split over the timedemo:

| Bucket | T&L on | Off |
|---|---|---|
| EngineDraw calls | 84,761 | 144,869 |
| EngineDraw total | 11.9 s | 15.0 s |
| EngineDraw per call | 140.9 µs | 103.7 µs |
| RingWrite per call | 58.5 µs | 40.2 µs |
| RenderDrain per call | 51.7 µs | 41.6 µs |
| RingWait per call | 9.5 µs | 4.8 µs |

Each batch costs more, because it carries several segments, but there are
41% fewer batches. Total engine time falls by 3.1 s over the 23 s demo.
The frame rate moves less than that, because the CPU still transforms
every vertex for the twin.

Each timedemo was run once on this build. Earlier pairs on this machine
repeated within about 0.7 fps (step 3 record: 84.6-85.3 off, 78.9-79.3
on), so the margins of 2.8 and
1.5 fps are above that spread. They are not large.

## What is left

- The CPU twin is still built for every captured primitive. Dropping it,
  with the host-tested fallback the step 3 record describes, is the
  largest remaining saving.
- `flush-segments-full` (2,427 in Half-Life, 1,758 in Quake 3) suggests 8
  segments is a limit worth raising. The vertex-area tail holds 8 slots of
  0x200 bytes, and the slot block's constant area sets the limit.
- Array capture of `glDrawElements` and `glDrawArrays` is still open.

## Not checked

- Direct3D: unchanged by this step; 3DMark was not rerun on boot 87.
- Ivy Bridge, a 32-bit desktop, two-unit captures and fog, as in step 3.
