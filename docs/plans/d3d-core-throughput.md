# Direct3D throughput in the shared core

Date: 2026-10-24

Status: **measuring.** Michael (2026-10-24): "after all that work we still
seem to be 50% in 3dmark than the native driver ... This is the focus and
I think it is part of the shared core, not just ati 128 specific." This
overrides the standing rule against chasing 3DMark scores, for this work.

## The gap, measured

A8U4I5: P3 1 GHz, 440BX, Rage Fury Pro, 3DMark 2000 build 340 default run,
1024x768x16, Z16, triple buffered. ATI is ATI 4.13.7192, 2026-10-22
([record](../decisions/2026-10-22-rage128-ati-baseline-and-tier0.md)).
Velocity9x is the CCE-ring build, boot 543, second run
([record](../decisions/2026-10-23-rage128-driver-runs-the-cce-ring.md)).

| Test | ATI | Velocity9x |
|---|---|---|
| 3DMarks | 2404 | 1249 (first run 1335) |
| CPU speed | 255 | 109 |
| Helicopter low / med / high | 42.6 / 32.8 / 16.1 | 23.3 / 18.8 / 7.1 |
| Adventure low / med / high | 41.0 / 37.9 / 29.9 | 24.1 / 17.5 / 12.3 |
| Fill rate single / multi | 128.6 / 178.2 | 129.6 / 129.6 |
| High polygon 1 / 4 / 8 lights (KTri/s) | 2341 / 2067 / 1864 | 689 / 665 / 640 |
| Texture rendering 8 / 16 / 32 MB | 136.4 / 134.1 / 121.2 | 59.8 / 59.5 / 58.4 |

The chip is not the limit: single-texture fill rate is ATI's.

## Hypotheses, from the numbers and the code, not yet measured

1. **Every flip serialises the CPU on the GPU, and the previous flip's
   latch.** `v9x_flip_body` (`ddhal_core.c`) works in four steps:
   - `v9x_wait_idle`, then `v9x_render_drain`, so the whole frame must
     finish before the flip;
   - then `v9x_flip_done`, which answers WASSTILLDRAWING until the
     previous start address is latched.

   The Rage 128 latches `CRTC_OFFSET` at the vertical blank even when the
   flip is written "now". The texture tests sit at 58-60 fps on a 59 Hz
   desktop, while ATI's run at 121-136. 3DMark called Flip 2,571,619 times
   for about 5,960 accepted flips. The CPU builds nothing while the GPU
   draws, and the GPU draws nothing while the CPU waits.
2. **Per-triangle CPU cost in the core.** High polygon count runs at
   about 30% of ATI's, and CPU speed at 43%. The D3D runtime does
   transform and lighting in both cases, so the difference is the
   driver's per-batch and per-triangle work: the `R3dList` clip, cull and
   mip checks, the vertex copies into the engine's stream, and the stream
   copy into the ring.
3. **Features ATI uses and we do not:** two textures per pass (multi fill
   rate equals single), emboss bump mapping, and the second 16 MiB.
   3DMark reports 16384 KB of video memory on a 32 MiB card.

## Measured, 2026-10-24

The Rage 128 is timed from boot 544. In the default run (388 s), the
buckets nest, so they do not sum:

| Bucket | Boot 544, uncached ring (1322) | Boot 545, write-combined ring (1444) |
|---|---|---|
| D3D entry points, everything inside | 162 s | 134 s |
| Ring write, total | 106 s | 70 s |
| of which waiting for ring space | 29 s | 43 s |
| Rage 128 decode (map and assembly) | 22 s | 24 s |
| Render drain (CPU waits for GPU) | 43 s | 45 s |
| Flip (2.7 M calls, about 6,000 taken) | 50 s | 52 s |

**Step done: the ring is write-combined**, as ATI's mini-VDD maps it:
reserve `PG_WRITECOMBINED`, commit with flags 0, and a locked instruction
before each `DL_WPTR` write. Uncached writes cost 60 cycles a dword;
write-combined writes cost 5.3 (`R128CCE /time`). VGARTD refuses a
write-combined commit. The reservation lands at `DA000000`, so
`AGP_BASE` is `DA000000` and the offset is 0: ATI's placement.

What is left:
- **The CPU waits for the GPU:** ring space (43 s) plus drains (45 s).
- **Flip:** 52 s.

Two causes, one shared and one the Rage 128's:
- **Shared core:** the flip serialises frame N+1's building behind frame
  N's drawing (hypothesis 1).
- **Rage 128:** each batch is at most 64 triangles (38 on average over
  50.4 M), and each ends with a GUI pixel-cache flush that stalls the
  engine, 1.33 M times in one run. Linux's r128 flushes at clears and
  swaps, not per primitive.

**Tried and dropped: no pixel-cache flush per batch** (boot 546). The
drain issued one flush through the ring instead. V9XD32 and the four
render probes passed. 3DMark 2000 scored 1454 against 1444, and the CPU's
wait for ring space was 42.3 s against 42.6 s. That is within run-to-run
noise, so the per-batch flush is not what holds the GPU, and the change
was reverted unmerged.

## Queued flips, done (Michael: "yes do queued flips", 2026-10-24)

- **Core:** `v9x_scanout_flip_queued()`. When it says yes, Flip skips the
  idle wait and the render drain.
- **Rage 128 flip:** on the CCE ring, the flip goes in as a pixel-cache
  flush, `WAIT_UNTIL` `EVENT_CRTC_OFFSET`, `CRTC_OFFSET`, and the flip's
  number to `GUI_SCRATCH_REG1`. The flip is pending until the scratch
  register holds that number and `CRTC_GUI_TRIG_OFFSET` is clear.
- **Rage 128 draws:** a draw into the retiring buffer while the flip is
  pending starts with a `WAIT_UNTIL`. The register guide says that bit
  stalls until the display shows the last offset written and does not
  stall when nothing is pending (RRG p. 3-238). It has no engine-idle
  triggers, so as in Linux, the offset write can pass a primitive still
  rasterising. The CRTC takes the offset only at the next blank.

A8U4I5 boot 547:
- V9XD32 passes.
- The render probes give their usual results.
- 3DMark 2000 scores **1496**:

| Bucket | Boot 545 (1444) | Boot 547 (1496) |
|---|---|---|
| Render drain | 45 s | 17 s |
| Flip | 52 s | 32 s |
| BltFill (the clear) | 2.8 s | 19 s |
| Ring space wait | 43 s | 44 s |

`DrawsFlipWaited` reads 0 (3DMark triple-buffers). There were no
timeouts, declines or forced flips.

**What this shows.** The per-frame clear is a 2D fill, and it now carries
the drain. More important, the CPU is waiting on the GPU in every bucket,
about 112 s of 419: the CCE is the limit.

Every triangle is three full 8-dword vertices, 24 dwords. High polygon
peaks at 689 K tri/s, about 16.5 M dwords/s through the microcode, against
ATI's 2.3 M tri/s. 3DMark makes 422 K indexed draw calls, and the core
expands every one into full triangle lists. ATI most likely sends indexed
primitives instead: `3D_RNDR_GEN_INDX_PRIM`, with the vertices in AGP
memory fetched by the vertex walker (mode 8's VCBM channel) and only
indices in the ring. That cost is the core's indexed path and the Rage
128's vertex submission, not the flip.

Next, in order:
1. Indexed primitives: the core hands an engine the index list and the
   vertex range instead of expanded triangles, where the engine says it
   can take them. On the Rage 128 that means the vertices go into AGP
   memory, as ATI's driver keeps them.
2. 2D fill and copy as CCE packets while the ring runs (`PAINT_MULTI`,
   `BITBLT_MULTI`), so the clear stops draining.
3. The per-engine batch size.

## Indexed primitives, done (Michael: "yes do indexed", 2026-10-24)

**Core.** A new optional engine op, `draw_indexed`. A whole
DrawOneIndexedPrimitive call goes to it: the vertex pool, the 16-bit
indices, a list or a strip, and the honoured cull mode. The engine may
decline, and the call is then expanded as before. Table fog always
expands. The new counters are `IndexedEngineCalls` and
`IndexedEngineDeclined`.

**Rage 128.**
- **Decline rules:** the engine declines anything the list path would
  treat differently. That is a vertex that is not finite, a textured one
  with `rhw <= 0`, one off the target after the +0.5 pixel centre, or a
  range larger than a 64 KiB segment.
- **Vertices:** the used ones are copied once into a 512 KiB AGP vertex
  area behind the ring. The mini-VDD's reservation grows to 145 pages and
  reports its full size in `ring_bytes`.
- **Packets:** `3D_RNDR_GEN_INDX_PRIM`, laid out as Linux's
  `r128_cce_dispatch_indices`, with the indices rebased and packed two to
  a dword.
- **Culling:** in `PM4_VC_FPU_SETUP`. D3D CCW is `FRONT_DIR_CCW`, back
  culled, following Mesa's y flip.
- **Ring mode:** 64BM_64VCBM_64INDBM, Linux's mode, so the drain waits
  for 64 FIFO entries.
- **Vertex reuse:** each segment closes with an age in
  `GUI_SCRATCH_REG2`, written through the ring. A segment is reused only
  once its age has come back, or after a drain.

V9XD32 gained T9 (indexed, CCW culling, one kept and one culled triangle,
a shared-edge quad). It passes on the old path (boot 547) and on this
build (boot 548). The runtime turns its small calls into DrawPrimitives
(`IndexedCalls` 0), though, so T9 does not reach the new entry.

3DMark 2000 does reach it. A8U4I5 boot 548 scored **1530**. The engine
took 259,490 indexed calls whole and declined 150,954; there were no
timeouts. The helicopter and Adventure scenes draw correctly
(`3dmark2000-*-b548-indexed.png`).

| Test | ATI | Ring (boot 543) | Indexed (boot 548) |
|---|---|---|---|
| CPU speed | 255 | 109 | 166 |
| Helicopter low / med / high | 42.6 / 32.8 / 16.1 | 23.3 / 18.8 / 7.1 | 32.7 / 24.8 / 9.7 |
| Adventure low / med / high | 41.0 / 37.9 / 29.9 | 24.1 / 17.5 / 12.3 | 24.6 / 20.5 / 15.3 |
| High polygon 1 / 4 / 8 lights | 2341 / 2067 / 1864 | 689 / 665 / 640 | 955 / 900 / 864 |
| Texture rendering 8 / 16 / 32 MB | 136 / 134 / 121 | 59.8 / 59.5 / 58.4 | 59.9 / 59.6 / 58.4 |

What remains:
- **37% of indexed calls are declined.** The Rage 128 clips in the core
  because its setup engine "has not been measured drawing a vertex off
  the target". Measure it within the 2048 guard band, and if it holds,
  accept those calls.
- **The texture tests sit at the 59 Hz refresh.** Our flips honour
  vblank; ATI's evidently do not in this benchmark. That is a policy
  question for Michael (`[Velocity9x] VSync`), not a speed one.
- The clear in the ring, and the core's per-call cost.

The ATI floppy's disk 1 went 408 bytes over. `V9XUPD.EXE` now rides a
third disk (`Floppy.ThirdDisk`; Michael: "third disk is fine"), which the
floppy INF names as source disk 2.

## Guard band, measured (Michael: "do guard measure", 2026-10-24)

V9XD32 gained T10, which draws three triangles that reach past the
target:
- a flat triangle with vertices at -1500 and +2000, which must fill the
  band at y 400-439 to both edges and leave nothing above it;
- a Gouraud triangle crossing three edges, sampled at three pixels;
- a triangle off the right edge, which must be drawn without wrapping
  into the left of the next row.

It ran on the core-clipping build (boot 548, reference), then on a build
where the Rage 128 draws them itself:
- `clip_in_core` 0;
- every Direct3D draw scissored to its target, where before it was
  opened to 8191, since the core had clipped;
- vertices accepted anywhere in the 2048 guard band on both draw paths;
- anything past the band dropped, as the core's clipper would.

On boot 549 every V9XD32 value matched the reference except a texture
handle. T10 passed, with the Gouraud samples equal (`A2E0`, `7BE0`,
`5500`), and `R3dListClipped` was 0.

3DMark 2000 scored **1568** (1530), with 0 triangles clipped in the core,
no timeouts and the frames correct:
- indexed calls declined: 150,954 to 114,337;
- indexed calls drawn whole: 306,060;
- high polygon 955 / 900 / 864 to 1004 / 945 / 901 KTri/s;
- Game 1 low 33.3 fps, Game 2 low 25.6 fps.

Still declined: 27% of indexed calls. The reason is not counted. Likely
candidates are a vertex range over a 64 KiB segment (2048 vertices) and
textured vertices with `rhw <= 0`.

## Declines counted (Michael: "count decline", 2026-10-24)

`IndexedDecline*` in the trace dump counts each declined indexed call by
reason, with the largest range declined and drawn. A8U4I5 boot 550, a
3DMark 2000 run with these counters (1606): Vertex 101,309, Range 13,217
(largest 5,894 vertices), Space 1, every other reason 0.

The Vertex declines come from the check covering every vertex from the
lowest index to the highest, including vertices no triangle names. A build
that checked only the referenced vertices (boots 551-552) declined none
for Vertex: 392,609 of 405,165 indexed calls went whole, about 65 times
as many as before. It did not change the score: **1529**. Its first run
(boot 551) locked the whole machine 35 s in, with no fault trace and the
agent log simply ending; the second completed. The cause is not known.
One unproven candidate: the vertex area ends exactly where the AGP
reservation ends, and the vertex walker may read past a segment's end as
the CCE reads past the ring's.

That build was not kept: the range check stays. More engine-drawn calls
did not move the score, so the CPU side's submission is not what limits
it; the chip is (the ring-space waits measured above).

## Proposal for Michael's review: flips queued behind the drawing

This changes flip behaviour in the shared core for every engine that
takes it.

1. **An engine hook that queues a flip in the command stream.** The
   Rage 128 writes `CRTC_OFFSET` with a type-0 packet after the frame's
   last batch, as Linux's `r128_cce_dispatch_flip` does. Intel's ring
   flip (`MI_DISPLAY_FLIP`, `CAP_FLIP_RING`) is the existing case. With
   the hook, `v9x_flip_body` skips `v9x_wait_idle` and
   `v9x_render_drain`, queues the flip, and returns. The CPU builds the
   next frame while the GPU draws this one.
2. **The GPU, not the CPU, waits before drawing into a buffer still on
   screen.** The first batch after a queued flip starts with the
   engine's wait for the flip to latch: the Rage 128's `WAIT_UNTIL`
   page-flip event, as Linux emits it, or `MI_WAIT_FOR_EVENT` on Intel.
   The GM965's ring flips tore 3DMark frames when they were tried
   (parked, branch `park/gen4-flip-chain-128k`). Drawing into the
   still-displayed buffer without that wait fits the symptom; that is
   unverified.
3. **Bounded depth:** Flip answers WASSTILLDRAWING only while the
   chain's last flip has not latched, so with triple buffering the
   CPU runs one frame ahead at most. Lock, a blit or GetFlipStatus on a
   surface in a queued flip drains first, as now.
4. **Engines without the hook keep today's path unchanged.**

Measured on A8U4I5 first, then on the netbook's Gen3, where a ring flip
already exists, before any default changes elsewhere.

Next, separately: a per-engine batch size in place of the core's fixed
64 triangles (`V9X_D3D_INDEXED_BATCH`, sized to Gen3). The Rage 128's
packet takes 256, and the ring would grow with it. Then the core's own
per-call cost, about 40 s of the run outside the engine.

## Steps

1. Time the Rage 128 (`V9X_TIME_ENABLED` on `ATI_RAGE128`). Use the
   existing buckets: D3D calls, decode (the Rage 128's map and assembly),
   ring write, ring space wait, render drain, flip, lock. Take one clean
   boot and one 3DMark run, and split the wall time.
2. Fix whichever of 1 and 2 dominates, in the core where the cause is
   there, so every engine gets it ([[same-design-fix-applies-to-all]]).
   For 1, that means flips queued behind rendering instead of the CPU
   waiting for both. For 2, that means less work per triangle on the path
   every engine shares.
3. Then the features in 3, on the Rage 128.

Each step is measured on A8U4I5 against the table above, and on a second
engine (the netbook's Gen3) for any core change.
