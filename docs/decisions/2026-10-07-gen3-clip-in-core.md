# Gen3 is clipped in the core, like every other engine

Date: 2026-10-07
Status: accepted

Evidence: [snapshots, frames and logs](../probe/netbook-ut99-gen3-clip-2026-10-07/),
all on the netbook (HP Mini 110, GMA 950), UT99 436 Direct3D at 640x480.

## Context

With 0.12.1's fan and strip fix, UT99 stopped losing its DrawOnePrimitive
fans on the netbook (29,142 of 29,142 refused on 0.12.0, 0 of 30,663 on
0.12.1), but the picture stayed broken: black jagged holes in the intro,
walls and floors in flat red and magenta in DM-Morpheus, the scoreboard
drawn over itself. The counters said why. Over one intro and DM-Morpheus run
the Intel engine refused 260,126 batches on 0.12.0 and 207,032 on 0.12.1,
every one `I9xxRefuseLast=6`: the vertex builder rejected the stream before
anything reached the ring.

The engine table (`d3d_i9xx.c`) set `clip_in_core = 0`, with the note that
Gen3 clips against its own drawing rectangle inside a 4096 guard band and
that 3DMark99 had run unclipped. The vertex builders it calls
(`i9xx_vertex.c`, `v9x_i9xx_build_runtime_run_common`) accept a vertex with
x in [0, width], y in [0, height], z in [0, 1] and a positive finite rhw,
and refuse the whole run otherwise; they say so ("the core clips before an
engine is called"). With `clip_in_core = 0` the list builder
(`r3d_clip.c`) skips the clipper entirely, so nothing clipped: one vertex
off the screen refused its batch.

It had been seen once already. At DDI 6, with a guard band published,
Half-Life's off-screen vertices produced 298,000 such refusals a run
(2026-10-06), and the fix then was to publish no guard band for an engine
the core does not clip for, so that the runtime clips. That covers DDI 6
only. UT99 sends pre-transformed vertices to the DX5 entry points, which
the runtime does not clip, and relies on the hardware's guard band.

## Options

1. Clip in the core for Gen3 (`clip_in_core = 1`), as for the Mach64, Rage
   II, SiS 6326, ViRGE and software engines.
2. Let the builders accept vertices inside the 4096 guard band and trust the
   drawing rectangle to cut them. Not measured on this part; the builders'
   bound is the memory-safety argument for the runtime path (a write
   outside the surface), and widening it is a claim about the hardware that
   nothing here has tested.
3. Leave it, as the remaining refusal on the Intel path.

## Decision

Option 1. One line in the engine table. The clipper cuts a crossing triangle
to the target, which is exactly what the builders accept, so nothing reaches
them that they did not already take.

| Netbook, UT99 intro + DM-Morpheus | 0.12.0 | 0.12.1 | core clip |
|---|---|---|---|
| DrawOnePrimitive refused | 29,142 of 29,142 | 0 of 30,663 | 0 of 19,420 |
| Engine batches refused (reason 6) | 260,126 | 207,032 | 77 |
| Triangles clipped by the core | 0 | 0 | 246,980 |
| DM-Morpheus, timedemo average | not read | 49 to 54 fps | 33 to 35 fps |

The lower frame rate is the card drawing the scene: at 0.12.1 most of it was
being thrown away. The frames show walls, floors, the weapon, the HUD and a
bot drawn as on the Rage XL; the intro has no holes.

`clip_in_core` also decides whether DDI 6 programs are told of a guard band
(`v9x_d3d_extended_caps7`). Gen3 now publishes one, as the Rage XL does, and
the core does the clipping the runtime did. Half-Life forced to DDI 6
(`[Velocity9x.Direct3DDdi] HL.EXE=6`), the case that produced the 298,000
refusals, now has none.

Checked for regressions on the same build:

| Netbook | before | core clip |
|---|---|---|
| Half-Life `mwd5`, DDI 5 | 42.0 fps (0.12.0 release) | 41.3, 41.2 fps |
| Half-Life `mwd5`, DDI 6 | not run | 41.0, 40.0 fps |
| Half-Life, new engine refusals | | 0 at DDI 5, 0 at DDI 6 |
| Quake 2 demo attract loop, OpenGL | 19.6 to 19.8 fps (2026-10-01) | 20.0 to 20.4 fps |
| Quake 2, two-unit batches refused | | 0 of 259,884 |

The OpenGL ICD clips to its drawable before the render interface sees a
triangle, so it is unaffected except that a two-unit draw is now held to
the target plus one pixel instead of the guard band; Quake 2's two-unit
draws all passed it.

## Consequences

- Every engine is now clipped in the core; `clip_in_core = 0` has no user.
- 77 batches a UT99 run are still refused with reason 6. They are not screen
  edges any more, so a z outside [0, 1] or an rhw that is not positive is
  the likely cause. Not measured: telling which needs a new counter in the
  diagnostics block. Nothing visible is missing in the frames.
- The Rage XL and the other engines are unchanged: the edit is the Gen3
  table's.
