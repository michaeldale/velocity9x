# Half-Life's OpenGL renderer from 10.75 to 14.75 fps: the ICD's own CPU work, measured and cut

Date: 2026-10-01
Machine: MICHAEL-NETBOOK, 945GSE / GMA 950, 1024x576x16 desktop, HAL and
driver set from `c44ebc3` (package B of the flip-tick record), boots 82.
Workload: Half-Life 1.1.1.0, `hl.exe -console -condebug -gl`,
`timedemo mwd5` (tests/benchmarks/hl1), one warm-up run then one measured
run per launch. Half-Life runs windowed here (a 640x480 window on the
desktop); its Direct3D renderer runs fullscreen.
Evidence: [`../probe/hl1-opengl-icd-2026-10-01/`](../probe/hl1-opengl-icd-2026-10-01/)

## Where it started

| Renderer | `mwd5` fps (measured run) |
|---|---|
| Direct3D, same session style | 26.19 and 28.84 (two launches) |
| OpenGL, ICD before this work | 10.75 |

HAL snapshots either side of each demo (`hlshare.py`): in the Direct3D
demo window the HAL is about 51% of wall time; in the OpenGL one, about 24%
(engine draw), with 3,721 Gen3 batches a second and no refusals or
software fallback. So most of the OpenGL frame was outside the HAL.

## The instrument

TSC buckets in `gl_icd.c` (RDTSC as opcode bytes, CPUID-guarded), reported
every ten seconds in `V9XGL.LOG` as a `tsc` line: `glVertex*` (including
any flush a full batch triggers), `glBegin`/`glEnd`, every pending flush,
inside it the window re-bind and the render interface's draw, the batch
sink with its texture/state prep, and the swap; plus call counts. The
interface draw was previously timed with `QueryPerformanceCounter`, twice
per draw.

## Three changes, each measured on the demo

| Build | What changed | measured run |
|---|---|---|
| before | `QueryPerformanceCounter` around every interface draw | 10.75 fps |
| `hlgl3` | that timing by RDTSC instead | **13.07 fps** |
| `hlgl4` | sink and sink-prep buckets added (instrument only) | 12.56 fps |
| `hlgl5` | texture lookup hint | **14.15 fps** |
| `hlgl6` | per-primitive window mapping, packed-colour cache, q = 1 skip | **14.75 fps** |

**The timer was the first cost.** On Windows 98 `QueryPerformanceCounter`
reads the 1.19 MHz PIT; two reads per interface draw at 15,000-20,000
draws per ten seconds cost about 18% of the frame. Removing them is the
whole of the 10.75 to 13.07 step.

**The batch sink re-described the texture for every polygon.** `glEnd`
hands each polygon to `v9x_gl_draw_batch`, which re-describes the bound
texture and the fragment state to decide whether it joins the held batch.
`v9x_gl_texobj_find` scanned the texture table linearly, each entry
carrying its ten-level array, for the name that was bound a polygon ago.
The prep measured 7.8 us per polygon (1.05 s per ten seconds, 135,048
polygons). With a validated index hint (`find_hint`, checked against
`in_use` and the name; an index, so the table moving when it grows does not
stale it) it measured 2.0 us (0.30 s, 149,169).
`tests/host/test_gl_texture.c` gains a test that the lookup stays right
through a freed slot reused by another name and through table growth.

**The vertex path redid per-primitive work per vertex.** The draw
rectangle and the viewport's integer-to-float conversions are now taken at
`glBegin` (viewport, scissor and depth range cannot change inside
Begin/End; a window resize seen by a flush mid-primitive applies from the
next primitive), the packed colour is reused while the four floats are
the same, and a q of 1 skips the texture divides. Each is exact; the host
vertex-pipeline tests pass unchanged. Per vertex: 2.35 us (`hlgl5`,
3.185 s / 1,357,923) to 1.89 us (`hlgl6`, 2.662 s / 1,410,724).

## Where the frame is now (`hlgl6`, a 126-frame interval, ~79 ms a frame)

| Bucket | per frame |
|---|---|
| `glVertex*` | 21.1 ms |
| batch sink (prep 2.9 ms of it) | 13.5 ms |
| pending flushes, of which the interface draw 21.6 ms | 24.3 ms |
| swap | 0.6 ms |

The buckets overlap where a flush happens inside the sink or a vertex, so
they do not sum to the frame. The interface draw - the HAL - is now about
the same cost per frame as Direct3D's whole HAL share.

## Not established

- Why the sink costs about 8.6 us per polygon beyond its prep; part of it
  is flushes nested inside it, which these buckets do not separate.
- Whether a combined modelview-projection matrix is worth its rounding
  change; not tried, because it is not bit-identical to the current
  transform.
- Anything about other GL applications. Quake 2 and the probes were not
  rerun.
- Whether the windowed presentation costs Half-Life anything against
  fullscreen; the swap bucket is under 1 ms a frame.
