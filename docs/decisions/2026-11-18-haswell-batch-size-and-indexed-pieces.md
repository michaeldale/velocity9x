# HD 4600: batches a slot long and indexed calls in pieces, 2001 SE 3388 to 3888

Date: 2026-11-18

Machine: the i5-4570 (HD 4600, `8086:0412`, W9QI5A83), 1024x768x32,
render clock at RP0 (1150 MHz). Builds: `bde7fc3b-dirty`, each installed as
the whole intel-gma set by `run-release-checks.ps1 -Machine hd4600`
(WININIT renames, `reset`, 8 of 8 hashes). Evidence in
`build/driver-results/` (not in the tree): `hsw-batch-b273-base`,
`hsw-batch-b274`, `hsw-prof-b274`, `hsw-idx-b275`, and the probe runs
`hsw-batch-checks`, `hsw-idx-checks`.

Follows [the GCN batch-cost record](2026-11-18-gcn-batch-cost.md), whose
first and third changes had Haswell counterparts.

## Where it started

Boot 273 ran the 0.55.0 code. 3DMark 2001 SE, as installed (the
selection the score dialog calls "not enough tests run for a 3DMark
score"), scored **3388**. V9XTRACE's buckets over the 8.4-minute run:

- 6.6 M engine batches at 20.5 us each, 10.5 us of it waiting for the
  other slot (`TimeRenderDrain`, 72 s);
- the shared core cut every batch at 64 triangles: the Haswell limits
  table never named `batch_triangles`, though a slot holds 298;
- `TimeD3dCalls` 294 s, of which the engine draw was 135 s.

## 1. Batches a slot long

`batch_triangles` is `V9X_D3D_HSW_BATCH_TRIANGLES`, 293: one slot's
one-unit triangles, less the room the piece loop keeps for a clipped fan.
The engine already split a longer draw into slot-sized pieces.

Boot 274: probes pass. 2001 SE **3539** with grabs, **3670** under
V9XPROF without; engine batches 6.6 M to 2.6 M. The slot wait did not
shrink (72.8 s): the GPU, not the batch count, holds the CPU there.

## Where the CPU went (boot 274, V9XPROF, score 3670)

| cost | share of samples |
|---|---:|
| the driver waiting on scanout, vsync and pending flips (`hsw_scanout`) | 16.5 % |
| waiting for the GPU to free the other slot (`v9x_d3d_hsw_wait_slot`) | 11.3 % |
| DXT level read back on Lock (`v9x_d3d_hsw_lock_surface`) | 5.4 % |
| the core's per-vertex work (DP2 walk, records, conversion, guard, cull) | about 14 % |
| KERNEL32 and WINMM, 3DMark's own | 36.8 % |

Over the profiled run 448 K indexed calls went whole and 507 K were
declined for their size: past the shared gather's 192 vertices, or past
one slot's 28 KB. 269 M of 461 M triangles went through the list path's
per-triangle copy.

## 2. Indexed calls whole, in pieces past a slot

The Haswell tables now name `indexed_pool_vertices` (8192) and
`indexed_two_units`, so the core hands `v9x_d3d_hsw_draw_indexed` its
compacted pools, two-stage calls with a pair per pool vertex.

- A call whose range and indices fit one slot goes as before, now with
  TL2 vertices when it has two units.
- A larger one goes in pieces (`v9x_d3d_hsw_piece`): whole triangles while
  their vertices and indices fit a slot, each piece's vertices only those
  it names, renumbered, a strip unrolled with its odd triangles turned
  back. Each piece is a batch with its own state, as the list path's
  pieces are.
- Still declined: flat shading, a vertex past half the guard band or not
  finite, and a call past 8192 pool vertices that needs pieces.

Boot 275: V9XD32 at DDI 5, 6, 7 and the default, V9XGLP, V9XGLIDP,
V9XR3DP, V9XWPRS and V9XFBCMP pass. 2001 SE with grabs **3888**, against
3539. 2.12 M calls drawn whole; 4,550 declined for size, 16,670 for the
guard band; list-path triangles 270 M to 109 M. The ten grabbed Car Chase
frames are whole, two-unit surfaces included.

| boot | build | grabs | score |
|---|---|---|---:|
| 273 | 0.55.0 code | no | 3388 |
| 274 | + batches a slot long | yes | 3539 |
| 274 | same, under V9XPROF | no | 3670 |
| 275 | + indexed pieces | yes | **3888** |

## Not established

- One run of each. The same build scored 3539 and 3670 (grabs against
  V9XPROF), so a run's noise is not small next to step 1.
- `DrawsIntoPresented` read 13 in the indexed run, as in both of the
  Oland's indexed runs; unexplained, and the grabs show no damage.
- Ivy Bridge shares the draw code and takes both changes; no Ivy Bridge
  has run them.
- The piece builder is static HAL code with no host test.

## Tried and parked: clears on the render engine

Every colour fill went to the blitter after the CPU waited for the render
engine to go idle; every depth fill (a render-engine quad already) waited
for both rings to go idle and then for its own batch. Boot 275's run made
33 K colour and 14 K depth clears.

Branch `park/hsw-rcs-clears` (`ade25330`): a 3D device's colour target in
5:6:5 or 32 bpp filled by a render-engine quad of its colour (a 5:6:5
value widened by repeating each field's top bits; 32 bpp as B8G8R8A8,
all four bytes written), and depth fills queued like a draw.

Boot 276, the whole intel-gma set: every release probe passes; V9XD32
`ZClearFar16` 307,200 (every pixel far), `T4ClearedBad` 0. 3DMark 2001
SE with grabs **3837**, against 3888; `TimeRenderDrain` 95.8 s against
92.9 s; frames whole. No gain, as with the Oland's clears alone: the
drains they made were time the CPU spent waiting for the GPU anyway.
Parked for several draws to a batch, where a blitter fill would close
the open batch.

Flip waits for the render engine too, but `TimeFlip` was 9.7 s a run, and
the next frame's first draw waits for the flip anyway with two buffers.
Not changed.

## What is left

The CPU waits about 93 s of the run for the GPU to free a slot. Every
batch ends with a CS stall, a render-target flush and a breadcrumb, and
the GPU drains between batches. The GCN gain came from not doing that
per batch; on Haswell that means several draws per batch with one
closing stall.
