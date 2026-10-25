# Haswell draws indexed calls whole with the hardware's cull; Gen3 takes neither the guard band nor an indexed path

Date: 2026-10-25

Status: Haswell `draw_indexed` implemented and measured on the 4570
(W9QI5A83, i5-4570, HD 4600 8086:0412, Win98 SE) at boots 77 (reference)
and 80. Ivy Bridge runs the same engine table and so takes it too,
unmeasured. Gen3 skipped, reasons below. The Haswell guard band is not
done: it waits on a shared-core question (below).

Evidence: [`../probe/haswell-indexed-cull-2026-10-25/`](../probe/haswell-indexed-cull-2026-10-25/).
The Rage 128 work this ports: 90abb40f (indexed calls whole), c696d43a
(its cull direction, wrong until V9XD32 T9 reached the engine), 09379ddd
(guard band), [d3d-core-throughput.md](../plans/d3d-core-throughput.md).

## Gen3: neither, and why

- **Guard band.** Intel published no 3D programming reference for Gen3
  (the Gen3 datasheets in the research folder cover the display and
  memory controller only; the public PRMs start at the 965). The two
  reference drivers never send a vertex outside the drawing rectangle:
  Mesa's i915 clips every primitive in software through TNL, and
  xf86-video-intel's SNA clips its rectangles. So whether Gen3 rasterises
  a vertex at -1500 exactly is not documented anywhere, and the only
  evidence available would be V9XD32 T10's three samples. The Gen3 path's
  memory-safety design - the vertex builders and the stream decoder that
  refuse any vertex off the target - rests on it, and 2026-10-07 turned
  this option down for that reason. It can still be measured the Rage
  128's way if Michael wants it.
- **Hardware cull.** The core already culls in the list path; the
  hardware's cull pays only with an indexed path that skips the core.
  Gen3's indexed form, `PRIM3D_INDIRECT_ELTS`, is used by no reference
  driver - Mesa uses `PRIM3D_INDIRECT_SEQUENTIAL` only - so a Gen3
  `draw_indexed` would rest on an unused, undocumented mode.

## Haswell: what the PRM licenses

- `3DSTATE_INDEX_BUFFER` (Vol 2b): 16-bit indices, the ending address the
  last byte of a cache line with the bytes after the last index zero (the
  Haswell note), MOCS 0 as the vertex buffer's.
- `3DPRIMITIVE` with Vertex Access Type RANDOM (Vol 2b, DW1 bit 8),
  TRILIST or TRISTRIP, base vertex 0.
- `3DSTATE_SF` Cull Mode (DW2 30:29, CULLMODE_BACK = 3) and Front Winding
  (DW1 bit 0, CCW = 1): the winding Direct3D discards is made the back
  face.
- Guard band, for later: Vol 7 "Vertex XY Clamping and Quantization":
  screen-space extent [-32K, 32K-1], and every object's bounding box
  within 8K; pixels outside the drawing rectangle, which is the target,
  are not drawn ("Drawing Rectangle Clipping").

## What changed

- `hsw_3d.c`: `v9x_intel_hsw_index_buffer`,
  `v9x_intel_hsw_3dprimitive_indexed`, `v9x_intel_hsw_sf_screen_cull`,
  and four fields appended to `struct v9x_hsw_draw` (`indices`,
  `index_count`, `strip`, `cull`). `cull` is the driver's own encoding
  with zero meaning none, because the hardware's zero is CULLMODE_BOTH:
  an unset field would discard everything. Every site that fills the
  struct field by field now sets them (HAL, host tests, the HSWMM probe).
  Host tests worked by hand from Vol 2b.
- `d3d_hsw.c`: `v9x_d3d_hsw_draw_indexed`. The vertices the indices name
  are copied once into the slot's vertex area with the +0.5 pixel centre
  the core's list path applies, the indices rebased after them, one
  submission. Declined, as the Rage 128's are, for anything the list path
  would treat differently: a second unit, flat shading (the list path
  copies the provoking vertex's colours, which a shared vertex cannot
  take), an index past the pool, any vertex of the range off the target
  or not finite (this engine is still clipped in the core), and a range
  plus indices larger than a slot's 28 KiB vertex area.

## Boots

| Boot | Build | |
|---|---|---|
| 76 | mixed set from 10-22 to 10-24 | dispbench baseline only |
| 77 | 5ca96ad4 (main + Gen3 WC; the Haswell is not opted in) | reference |
| 80 | 5ca96ad4 + this change, built dirty before the commit | indexed |

Deployed by WININIT rename, restarts by `reset`. Agent updated from
0.12 (dev-012c) to 0.13.1 at boot 77 by its hot `update`.

## Measured (Haswell, 800x600x16 desktop)

| | Boot 77 | Boot 80 |
|---|---|---|
| V9XD32 /ddi6 | PASS, every path `core-list` | PASS; T9, T11 Indexed and IndexedWide, T13 Indexed `engine-indexed` |
| V9XD32 /ddi5 | PASS | PASS; T13 Indexed `engine-indexed` |
| V9XD32 pixel values | | every one equal to boot 77's |
| DB3D probes (Cull, Cube included) | 174 pass | 174 pass |
| Half-Life mwd5, Direct3D, windowed | 65.7 fps | 65.3 fps |
| Half-Life mwd5, OpenGL, windowed | 79.1 fps | 78.9 fps |
| 3DMark 2000 default (1024x768x16) | 1959 | 2032 |

Trace counters over each boot's runs (V9XD32, dispbench, Half-Life,
3DMark 2000):

| | Boot 77 | Boot 80 |
|---|---:|---:|
| Indexed calls (DrawOneIndexedPrimitive) | 528,719 | 545,019 |
| drawn whole by the engine | 0 | 392,456 |
| declined: range over a slot | | 61,107 (largest 5,894 vertices; largest drawn 890) |
| declined: a vertex off the target | | 81,967 |
| declined: state (flat shading, or the map refused) | | 9,552 |
| Core list calls | 1,920,139 | 1,343,907 |
| Triangles culled in the core | 3,523,099 | 214,204 |
| Engine, ring, breadcrumb and flip-wait timeouts | 0 | 0 |

Readings:

- **The cull direction was right on the first build.** T9 (one kept, one
  culled, a shared-edge quad) and T11's cull matrix reached the engine
  at DDI 6 and drew what the core draws; DB3D's 54-case Cull check and
  Cube pass.
- **3DMark 2000 moved 3.7%; Half-Life did not.** Half-Life's indexed
  calls are not counted separately here; its timedemo is unchanged either
  way. The Haswell's limit is the GPU's cost per batch
  ([2026-10-23](2026-10-23-haswell-gl-draws-run-on.md)), and a whole call
  is one batch where the expansion made several.
- **28% of indexed calls are still expanded.** Off-target vertices are
  the largest share and are what the guard band would take; ranges over
  a slot are the next, and need either a larger vertex area (the reserve
  is a mini-VDD contract) or the call split by index runs.

## What is left open

- **The guard band.** With `clip_in_core` 0 the core publishes a zero
  guard band at DDI 6 (`v9x_d3d_extended_caps7`), so the runtime clips
  every DX8/DDI 6 triangle on the CPU. That is the Rage 128's state since
  09379ddd. Changing the core to publish the band for an engine that
  draws it changes the Rage 128's DDI 6 behaviour, untested; asked of
  Michael before doing either.
- **Frames not watched.** 3DMark 2000 ran full screen, which the agent's
  screenshot cannot see, and OBS is not connected to this machine. Its
  correctness rests on the probes above.
- **Half-Life faulted once at launch** on boot 77 (EIP 0, unknown module;
  screenshot in the evidence), before any change of this record was
  deployed, and started on the retry. Recorded, not chased.
- **Ivy Bridge** shares `v9x_d3d_engine_hsw` and these builders; that its SF and primitive
  layouts match was not checked against the Ivy Bridge PRM, and it was not run.
