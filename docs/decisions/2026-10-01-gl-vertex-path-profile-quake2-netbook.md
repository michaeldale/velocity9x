# Where an OpenGL vertex's time goes in Quake 2: clipping a quarter of the triangles costs more than transforming all of them

Date: 2026-10-01
Machine: MICHAEL-NETBOOK, GMA 950, Atom N280 at 1.66 GHz, boot 87, async
submission on (default). Only the ICD changed: the profiling `V9XGL.DLL`
was written over the installed one with no OpenGL program running, and its
hash checked.
Evidence: [`../probe/gl-vertex-profile-2026-10-01/`](../probe/gl-vertex-profile-2026-10-01/)

## Why

With asynchronous submission, Quake 2's render-interface draws are 11% of
its time and the ICD's own work dominates
([record](2026-10-01-intel-async-corruption-was-the-dword-head.md)).
`glVertex*` alone was 34-37%: about 2.3 us a vertex, roughly 3,800 cycles,
for work that is a few hundred. None of the chips this driver runs has a
transform unit (Gen3, the Mach64 and the ViRGE take screen-space vertices),
so the vertex path is the lever, and it is shared by every engine.

## The instrument

`V9X_GL_PIPELINE.profile`: when the ICD supplies the array (the host tests
never do), `v9x_gl_prim_vertex` charges TSC cycles to five stages, and the
triangle assembly counts triangles taken whole, clipped and culled. A
`prim` line follows each ten-second `tsc` line in `V9XGL.LOG`. The marks
are five RDTSCs a vertex; the timedemo ran at 18.4-19.1 fps against 19.6-
19.8 without them, so the stage figures include about 4% of their own.

## Quake 2 demo2 under timedemo 1, 640x480 fullscreen (`q2-timedemo.ps1`)

Mean of the twelve ten-second intervals with over 100 frames, about 1.51
million vertices and 202 frames each:

| Bucket | per 10 s | of wall | per vertex |
|---|---|---|---|
| `glVertex*`, whole | 3,724 ms | 37.2% | 2.47 us |
| - transform (two matrices, colour, texcoord) | 434 ms | 4.3% | 288 ns |
| - inside test (ten planes) | 694 ms | 6.9% | 461 ns |
| - window mapping and emit | 570 ms | 5.7% | 378 ns |
| - assembly: triangles, clip, cull, batch | 1,468 ms | 14.7% | 975 ns |
| - history copies (`first`, `previous[3]`) | 342 ms | 3.4% | 227 ns |
| `glBegin`/`glEnd` | 1,645 ms | 16.4% | |
| - batch sink, called from `glEnd` (prep 427) | 1,463 ms | 14.6% | |
| flushes outside a vertex (render interface 1,218) | 1,516 ms | 15.2% | |

Triangles: 453,095 taken whole, **200,280 clipped (23%)**, 200,257 culled
(23%). About 172,000 sink calls, 14,227 render-interface draws.

## What it says

- **Clipping is the largest single stage.** Assembly is 975 ns a vertex,
  and a whole triangle's share of it is a few struct copies; the 23% that
  clip run Sutherland-Hodgman over all ten planes (six frustum, four draw
  rectangle), copying ~100-byte `V9X_GL_VERTEX`es into a scratch polygon
  and back at every plane, crossed or not. A triangle crossing one edge of
  the screen pays for ten. Spread over the clipped triangles, assembly is
  of the order of 6 us each; not separated from the whole-triangle path.
- **The inside test costs more than the transform**: ten calls of
  `v9x_gl_prim_plane`, a switch per plane, 461 ns against 288 for both
  matrix multiplies.
- **The history is 227 ns a vertex** of struct copies, three to four
  ~100-byte vertices per call.
- **The transform is not the problem.** Both 4x4 multiplies are 288 ns.
- **Begin/End is mostly the sink**: Quake 2 draws its world as one
  `GL_POLYGON` per surface, so about 172,000 sinks in ten seconds at 8.5 us
  each, 2.5 of them its texture/state prep.

## Exact candidates, by size

1. Clip only against the planes a triangle's corners actually cross, from
   outcodes taken during the inside test; a plane every corner is inside
   returns the polygon unchanged, so skipping it is exact. Ping-pong two
   buffers instead of copying back at each plane.
2. The inside test as outcode arithmetic in line rather than ten calls.
3. The history as indices into a small vertex ring rather than copies.
4. The sink's per-polygon cost, which needs its own split.

Not exact, and a policy question: a guard band - leaving the four draw-
rectangle planes to the hardware's scissor or drawing rectangle - would
remove most clipping outright, but the Gen3 stream allowlist refuses a
vertex outside the surface, and what each engine clips to has not been
measured.

## The first three, done (same evening, same boot)

Exact by construction and by test: `test_pipeline_output_unchanged` hashes
every byte the pipeline emits for a fixed pseudo-random stream - every
assembled mode, smooth and flat, culled and not, scissored and not,
projective texture coordinates, 609 triangles taken whole, 798 clipped,
188 culled - and holds it to the value taken from `1366659`'s pipeline,
`0x0ed372e9` over 5,763 vertices. It passed after each change.

- **Clipping:** each plane's distances once per vertex, kept as floats
  (`v9x_gl_prim_plane` rounds its result to float on return - `fstp` and
  `fld` in the object - so keeping them changes nothing); a plane every
  vertex of the current polygon is inside is skipped; passes alternate
  between two buffers.
- **Inside test:** `v9x_gl_prim_inside`, the ten distances written out,
  each stored as a float before its comparison.
- **History:** a four-slot vertex ring in the pipeline replaces the
  `previous[3]` copies; `first` is still copied once a primitive.

Quake 2 demo2, ICD alone replaced again (hash-checked), last session's
twelve demo intervals (`glstages.py`):

| | before (`q2prof`) | after (`q2fast`) |
|---|---|---|
| timedemo, timers on | 18.4-19.1 fps | **20.7-21.0 fps** |
| `glVertex*`, per vertex | 2,472 ns | **1,862 ns** |
| - transform | 288 | 280 |
| - inside test | 461 | **255** |
| - window and emit | 378 | 381 |
| - assembly | 975 | **708** |
| - history | 227 | **53** |
| `glVertex*`, of wall | 37.2% | 31.1% |
| `glBegin`/`glEnd`, of wall | 16.4% | 17.5% |

Both columns carry the stage timers' own cost; the unprofiled ICD before
the change ran 19.6-19.8 fps.

## The batch sink and glBegin, split (`q2sink`)

Six more ICD buckets: `glBegin` alone, and in the sink the held batch
drawn because the polygon differs or the batch is full, the hold and
vertex copy, and the prep's texture description, interface state and
same-draw compare. Twelve demo intervals, about 185,000 sinks and 15,351
render-interface draws per ten seconds; timedemo 20.3-21.0 fps.

| per sink (one polygon) | us | of wall |
|---|---|---|
| sink, whole | 9.27 | 17.1% |
| - the held batch drawn (a flush) | **5.67** | 10.5% |
| - prep: texture description | **1.88** | 3.5% |
| - prep: interface state | 0.41 | 0.8% |
| - prep: same-draw compare | 0.50 | 0.9% |
| - hold and vertex copy | 0.42 | 0.8% |
| `glBegin` alone | 1.65 | 3.0% |

So most of the sink is not the sink: it is the render-interface draw of
the batch the polygon cannot join, about twelve polygons a draw. In the
HAL over the same window (`hlprof.py`), a draw is 84.1 us: 38.4 writing
its ~850 dwords into the ring through the uncached aperture, 10.7
decoding, 8 building, 9 in the front end; and 5,774 breadcrumb drains at
Lock - Quake 2's lightmap uploads - cost 558 us each, 2.6% of the
window, with 1.8% more inside the locks.

What the ICD's own share points at, exact first: describe the texture
only when the binding or its parameters changed (1.88 us a polygon);
take glBegin's per-primitive setup only when viewport, scissor or depth
range changed (1.65 us). Outside the ICD: write-combining for the ring
and texture writes, and a lightmap upload that does not drain the GPU.

## Not established

- Per clipped triangle cost, which the stages do not separate.
- The remaining ~45% of wall time outside these buckets: Quake 2 itself,
  OPENGL32's dispatch into the ICD, and the other GL entry points
  (`glTexCoord`, `glColor`, state calls), which are not timed.
- Half-Life's OpenGL mix; same code path, not rerun with this build.
