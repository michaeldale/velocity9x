# HD 4600: OpenGL T&L without the CPU twin, 16 segments, an element cache

Date: 2026-10-26. Machine: the i5-4570 (W9QI5A83, HD 4600), boot 88,
800x600x16 desktop (565). Follows
[`2026-10-26-haswell-tnl-step3b-segmented-batches.md`](2026-10-26-haswell-tnl-step3b-segmented-batches.md),
which left three things open. Evidence:
[`../probe/hsw-4570-tnl-step3c-2026-10-26/`](../probe/hsw-4570-tnl-step3c-2026-10-26/).

## What changed

- **No CPU twin.** A captured primitive is no longer also transformed,
  clipped and windowed for the screen.
  - Each vertex goes through P x MV once (cached until either matrix
    changes) to clip space and gets a six-plane outcode.
  - A triangle every corner of which is outside one frustum plane is not
    sent.
  - Under `CULL_FACE`, a triangle facing away is not sent either, when all
    three w are positive. Its facing is the sign of the determinant of
    the corners' (x, y, w) rows. With a corner behind the eye the
    triangle goes whole to the engine's clipper.
  - Vertices are sent only when a triangle that survives names them.
- **Fallback without a twin.** A batch the engine refuses is drawn by
  `v9x_gl_prim_tnl_screen`, from the held object vertices under the
  constants and frame they were captured with.
  - Host-tested against the CPU path: perspective, offset viewport,
    scissor, narrowed depth range, back-face cull, a near-plane cut.
  - Never exercised on the machine: 0 refused in every run below.
- **q other than 1 part way through a primitive.** The capture ends there.
  The kept vertices are rebuilt for the CPU path from their object copies.
- **Scissor (a bug fixed).** Captured batches now carry the draw
  rectangle (the drawable, cut to the scissor box when the test is on) as
  their scissor.
  - Before this, a GL scissor narrower than the viewport was ignored on
    the T&L path: the CPU path cuts geometry to it, and the T&L path asked
    the engine for the whole drawable.
  - No game measured here uses such a scissor.
- **16 segments.** A segment's commands are 39 dwords and the draw's own
  212 (`test_vs_segments_fit`), so 16 do not fit the slot's 2 KB batch
  area. Indexed T&L batches are now built in the last 4 KB of the slot's
  vertex area, beside the later segments' state and push constants, 180h
  each.
  - `V9X_R3D_ABI_TNL_VERTICES_MAX` falls from 512 to 480 so the vertices
    and indices still fit beside them.
- **Element cache.** Within one `glDrawElements` call, a captured element
  is fetched and transformed once, however many indices name it (512
  slots, element mod 512).
- **Test runner (a bug fixed).** `v9x_run_gl_prim_tests` reset its failure
  count after the T&L tests ran, so step 3's T&L tests could not fail the
  gate. The reset is now first.

## Correctness

V9XGLP's A/B scene, which includes six alternating-texture quads, gives
the same numbers as at boots 85 and 87:

- 1,800 pixels differ by more than 3/255, 112 by more than 8/255, and none
  by more than 16/255. The worst channel is 9/255.
- Nothing is drawn outside the viewport.
- `Result=PASS`.

Half-Life and Quake 3 ran to the end of their demos with 0 refused and
0 failed.

## Speed

Every run is from boot 88. "Off" is `V9XGL_TNL=0`, the same build's CPU
path.

| Run | T&L on | Off |
|---|---|---|
| Half-Life `timedemo mwd5`, windowed 640x480 | 78.5, 95.9, 95.5, 93.9 | 84.7, 84.4, 84.9 |
| Quake 3 demo001, `multiple glArrayElement` (its default here) | 61.1, 61.1 | 57.2 |
| Quake 3 demo001, `+set r_primitives 2` (`single glDrawElements`) | 69.1 | 56.1 |

At boot 87, before these changes, the figures were Half-Life 88.0 against
85.2 and Quake 3 58.6 against 57.1.

What the counters say:

- **Half-Life.** The CPU's cull kept 1,040,776 triangles from the engine
  and sent 446,570, in 2,774 batches (4,048 at boot 87). Batches flushed
  because 16 segments were full: 891, against 2,427 at 8.
- **Quake 3 through `glDrawElements`.**
  - 6,859,597 vertices came from the element cache instead of being
    fetched and transformed again.
  - The run joined almost nothing in the hold (5,200 against 935,140 for
    the `glArrayElement` run). Each `glDrawElements` call is one Begin/End
    and one capture batch, so the merging step 3b relied on is not needed
    there.
- **Quake 3's batch count barely moved:** 30,800 against 31,894 at boot 87.
  Its breaks are frame and screen-batch flushes (12,615 and 12,706), not
  full segments (167).

### Not explained

The first Half-Life T&L run after the boot gave 78.5. The three after it
gave 93.9 to 95.9, and no off run fell outside 84.4 to 84.9. A traced
repeat put EngineDraw at 99 µs a call over 104,916 draws, most of which
are the CPU path's: Half-Life's world has two texture units and is not
captured. The trace of the slow run was not taken.

## Direct3D

The HAL changes are shared, so 3DMark was run again on boot 88:

| Benchmark | Boot 88 | Boot 84 |
|---|---|---|
| 3DMark 2000 Pro, default run | 2342 | 2346 |
| 3DMark 2001 SE (this machine's saved non-default settings) | 1109 | 1112 |

Neither uses T&L.

## What this leaves

- **Quake 3 does not use `glDrawElements` by default here.** It chooses
  `multiple glArrayElement` because the ICD does not offer
  `EXT_compiled_vertex_array` (parked in the plan).
  - Offering it, even with Lock and Unlock doing nothing beyond what the
    specification requires, would move Quake 3 to the 69 fps path.
  - Not done here.
- **Two-unit primitives are still the CPU's.** That is most of
  Half-Life's world.

## Not checked

- The engine-refusal fallback on hardware.
- A GL scissor narrower than the viewport on hardware.
- Ivy Bridge, a 32-bit desktop, fog and two-unit captures.
