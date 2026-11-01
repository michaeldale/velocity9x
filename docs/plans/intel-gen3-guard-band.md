# Intel Gen3: measure whether the GMA 950 can draw inside a guard band

Date: 2026-11-01. Status: approved the same day with a separate probe
(decision 3); decisions 1 and 2 as proposed. Step 1 done
([record](../decisions/2026-11-01-gen3-guard-band-step1.md)): no write
left the drawing rectangle at any distance to 3,968 pixels; Gouraud
colour holds to 2,048 and fails at 3,968, so the band is 1,024. Step 2
in part ([record](../decisions/2026-11-01-gen3-guard-band-step2.md)):
Half-Life and 3DMark 2000 clean under the band on fresh boots; one MPREXE
fault on the first run did not reproduce; UT99 and 3DMark 2001 SE not run.

Machine: the netbook (945GSE / GMA 950, Atom N280). Its `reset` verb
recovers a wedged render engine; nothing else may run this.

## Why

The [front-end profile](../decisions/2026-11-01-gen3-core-front-end-profile.md)
puts clipping at 9.2 µs of a 44 µs Half-Life submission, the largest cost
left before a Gen3 draw: 60 % of list calls clip. The core clips for Gen3
because the vertex builders (`i9xx_vertex.c`) and the stream decoder
(`i9xx_3d_decode.c`) accept only vertices on the target, and they do so
because a vertex off the target was taken to risk a write outside the
surface, in stolen memory that also holds the ring and the status page
(`0bdf0c1a`; [2026-10-25](../decisions/2026-10-25-intel-indexed-draws-and-hardware-cull.md)
turned a guard band down for want of documentation).

What is known:
- Every Gen3 batch already sets `3DSTATE_DRAW_RECT` to the target, and the
  decoder requires exactly that rectangle. Mesa's i915 sets it the same
  way. Whether Gen3 confines writes to it for vertices far outside it is
  not documented anywhere public.
- The HD 4600 draws inside its band correctly, on its PRM's promise
  ([2026-10-25](../decisions/2026-10-25-haswell-guard-band.md)).
- The Rage 128 drew T10 correctly inside its band, and still corrupted
  data files or hung under 3DMark 2001 SE
  ([2026-10-25](../decisions/2026-10-25-rage128-ddi6-guard-band-withdrawn.md)).
  **T10 passing is not evidence of safety.**

## What the probe must show

1. **No write outside the drawing rectangle**, for vertices up to a
   distance d off it, on every side. This is the memory-safety question
   and decides everything else.
2. **The pixels inside match the clipped reference** (edges within the
   rasteriser's own rounding, Gouraud and texture coordinates as T10
   samples them).
3. **No hang**, at each d.

The largest d that passes all three, halved for margin, is the band. If
d = 64 fails, the plan ends with a record.

## Step 1: the probe, measurement-only

Proposed shape, contained in the Gen3 files except where noted:

- **A variant bit, 0x100 GUARD**, in the mask `V9XTRACE -omit:<mask>`
  already sets (zero at boot; zero is the shipped path), with d in pixels
  in bits 16-27.
- **Under it, the drawing rectangle is inset by 32 pixels** on every side
  of the target, and the decoder accepts exactly that inset rectangle. The
  32-pixel frame inside the surface becomes the canary: any pixel written
  there was outside the drawing rectangle. A write past the surface lands
  in the frame first for any rasteriser that writes contiguous spans; for
  one that does not, the probe also checks the surfaces allocated either
  side of the target.
- **Under it, the vertex builders and the decoder accept a vertex within d
  of the inset rectangle** instead of on it.
- **Under it, the core does not clip for Gen3** triangles inside the band.
  `v9x_d3d_i9xx_limits` is `const` today and `clip_in_core` is read by the
  core at list setup; how the bit reaches that read is step 1's first
  design point.
- **A probe test**: the target cleared to a guard colour; for each d in
  64, 256, 1024, 2048, 4000, triangles reaching d past each edge and
  corner, flat, Gouraud and textured, drawn and read back. Results: frame
  pixels changed (must be 0), neighbour surfaces changed (must be 0),
  interior mismatches against the clipped reference, and the HAL's trace
  (timeouts, refusals). Then V9XD32 `/ddi6` and `/ddi5` with the bit set.

Order of runs: d = 64 first, and each larger d only after the smaller one
passed all three. A hang is followed by `reset` and recorded; that d
fails.

## Step 2: if a band passes, games before shipping

With the bit set at the measured band: 3DMark 2000, Half-Life mwd5 D3D
(the clip counters and µs a submission against the front-end profile),
UT99's intro (the DX5 off-target case of `0bdf0c1a`), and 3DMark 2001 SE
(the Rage 128's failure). Any corruption, hang or refusal stops it.

## Step 3: ship (its own plan section, after step 2)

`clip_in_core` 0 for Gen3 with a classifier like the HD 4600's: inside
the band drawn as given, beyond it clipped by the core's clipper. The
decoder's licence widened to the band permanently. The GMA 900 and GMA
3150 take it by the same-design rule.

## Decisions for Michael

1. **The decoder's licence widened under a measurement bit.** The decoder
   is this engine's memory-safety boundary; step 1 relaxes it, bounded by
   the inset rectangle, on the netbook only, with the mask zero at every
   boot.
2. **An appended field in the decoder's limits** (the band, and the inset
   rectangle), which the 16-bit scene callers share
   (`intel_3d16.c`, `intel_exec16.c`); they would pass zero.
3. **Where the probe test lives**: a new V9XD32 test, or a small separate
   probe. V9XD32 keeps one place for Direct3D checks; a separate probe can
   set the mask itself and cannot disturb V9XD32's existing results.

## Not in this plan

- Making the CPU clipper itself cheaper (`v9x_r3d_triangle_on_target` is
  1.9 µs, `v9x_r3d_draw_list` 2.6). It needs no hardware claim, and is the
  fallback if the band fails.
