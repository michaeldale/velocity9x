# The Direct3D block splits into a chip-neutral core and one engine, and the probe cannot tell

Date: 2026-08-29
Branch: `s3-device-id-aliases` - this work is unrelated to that branch's
subject and should have had its own; recorded as it happened rather than as
it should have been
Plan: [`family-structure-and-next-d3d-roadmap.md`](../plans/family-structure-and-next-d3d-roadmap.md) Track B

Guest: `Win86SE`, s3 family, ViRGE/DX 86C375, 4 MiB, agent port 9869.
Gate: `V9XDDP.EXE` per the
[2026-08-13 handoff](../handoffs/2026-08-13-hellbender-d3d-review.md) §2.3.

## Status against the roadmap

Track B is done, and it was executed **out of order**. The roadmap schedules
the split "immediately before Track C's D3D phase", on the house rule against
speculative refactors, and the reason is real: an abstraction drawn around one
engine is an abstraction shaped by one engine. It was brought forward on the
user's instruction. What follows records the boundary and the two rules
precisely enough that the second engine can argue with them from evidence
rather than re-deriving them from a monolith.

## What moved

`src/display32/d3d/d3d_virge.c` was 1,733 lines carrying both halves. It is
now:

| File | Lines | Holds |
|---|---|---|
| `d3d_internal.h` | 161 | the context and texture structs, `V9X_D3D_ENGINE_LIMITS`, `V9X_D3D_ENGINE_OPS`, the one core service an engine may call |
| `d3d_core.c` | 1,171 | context pool, texture handle table, render-state bookkeeping, software clipper, fog/specular vertex colour, all sixteen `V9xD3d*`/`V9xHal*` DDHAL entry points, callback wiring |
| `d3d_virge.c` | 679 | the S3D triangle emitter, the fixed-point converters, the sampler's format test, the device caps, and the chip's limits as data |

All 63 MMIO writes are in the engine. The core names no chip's register
vocabulary at all.

Everything moved is **byte-identical** to what passed the baseline gate: the
split was performed by a script copying verified line ranges, not by retyping.
What changed is only where the ViRGE's own numbers come from.

## The seam, and the two rules

**Rule 1 — batch granularity.** `draw_triangles(context, vertices,
triangle_count)`. Never a single triangle, never anything at register level.
The ViRGE is immediate-mode and would have been happy with a per-triangle
call; every plausible next engine — 3dfx FIFO, ATI Rage setup, Intel
ring/batch — is a command-stream engine that needs to see a run of work to
build one packet from. Fixing the granularity now means that engine moves no
seam.

The three call sites map onto it cleanly, which is itself evidence the
granularity is natural rather than imposed:

* `V9xD3dDrawPrimitives` already had a contiguous triangle list per record —
  one call, up to 64 triangles.
* `V9xD3dDrawOnePrimitive` — one call, one triangle.
* `V9xD3dRenderPrimitive` fans each clipped polygon; the core now materialises
  the fan into a list and makes one call of up to six.

**Rule 2 — behaviour-change-free.** Measured, see below.

**The third thing, which the roadmap did not anticipate:** most of what is
chip-specific in this module is not code, it is *numbers* — and they were
scattered through routines that are otherwise chip-neutral. The render target
must be 16 bpp; its pitch is 8-byte aligned with an `0FF8h` ceiling; width and
height cap at 2048; textures are square powers of two from 4 to 512 texels;
screen coordinates outside ±2048.0f overflow the 12.20 fixed-point converter.
Every one of those was a literal inside what is now the core.

They became `V9X_D3D_ENGINE_LIMITS`, a struct, rather than five more function
pointers. That is a deliberate call: a second engine changes these values and
nothing else about how they are used, and data is cheaper to get right than a
callback. If an engine ever needs to *reason* rather than *state* — a limit
that depends on the current mode, say — that is the point to promote the field
to an op, and it should be done then rather than pre-emptively.

## The first attempt broke Direct3D, and the evidence said it had not

Recorded because the failure and the false evidence are the useful part.

The split as first written selected the engine inside `v9x_d3d_publish` with
`v9x_d3d_engine()`, the same selector the draw path uses. **DriverInit runs
before the 16-bit side fills the engine descriptor** — `dd16.c:405` says
exactly that about the framebuffer descriptor, and `dd16.c:313-334` fills the
engine in that same later step. So at publish time `engine_type` is 0 and
`engine.flags` carries no `V9X_DD_ENGINE_VALID`, the selector returned null,
`v9x_d3d_publish` returned early, and the D3D tables were never filled.
Measured on the ViRGE guest: `D3DHalFound` 1 → 0, `TexFormatCount` 2 → 0,
`D3DDeviceCount` 4 → 3, and every Direct3D pixel test gone.

**The first run of this gate did not catch it, and reported the opposite.**
The 2026-08-13 handoff says the probe writes `C:\V9XDD.INI`; it has since
moved to `C:\V9XDIAG\V9XDD.INI`. Fetching the documented path returned a
stale file left by a build from commit `fba2f0c`, unchanged by either run — so
"byte-identical before and after" was two fetches of the same old file. The
`Build=` key is what exposed it: `fba2f0c-dirty` when the tree was at
`2f65e3e`.

Three things now guard against repeating it, and they are cheap:

* delete the result file before the run, so an unwritten file cannot pass as a
  result;
* check the `exec` exit code instead of discarding it;
* read the `Build=` key and require it to match the build under test.

The handoff's path is stale and is not corrected here — it is a dated record of
what was true then. Anyone running that gate should take the path from
`tools/diag/ddraw_probe_win32.c`.

## The fix

Caps publication cannot be chip-selected at DriverInit, because nothing has
told the 32-bit side which chip it is yet. `v9x_d3d_publish_engine()` therefore
returns the one D3D engine the binary carries, which is exactly the pre-split
behaviour: the tables were always filled, and the 16-bit side is and remains
the capability authority that hides them — `dd16.c:490` nulls `GetDriverInfo`,
both `lpD3D*` pointers and `lpDDExeBufCallbacks` for a family whose
`engine_caps` lack D3D.

`v9x_d3d_engine()` survives as the **draw-time** selector, where it is correct
and where the descriptor is valid: every entry point declines when it resolves
nothing. So the second gate the split was meant to add does exist — at call
time, which is the point that matters — it just cannot be the publish-time
gate as well.

A second D3D engine has to solve the publish-time selection properly, and the
fix belongs on the 16-bit side: stamp the chip's `engine_type` into the shared
block before DriverInit is called, then select on it. That changes the enable
ordering and needs its own evidence, so it is not guessed at here.

## Measured, on the corrected gate

Pre-split baseline built from a clean worktree at `2f65e3e` and installed on
the ViRGE guest; post-fix build at `764ed5a-dirty` installed over it. Both runs
deleted the result first and checked the exit code and the `Build=` stamp.

Every functional key is identical, rendered pixels included:

```
Result=COMPLETE          D3DHalFound=1            D3DCreateDeviceHr=0x00000000
D3DTrianglePixelRaw=31744  D3DTrianglePixelOk=1   D3DSubpixelTriangleOk=1
D3DBaseTextureRaw=992      D3DBaseTextureOk=1     D3DSpecularGouraudOk=1
D3DMipmapLevelRaw=31       D3DMipmapLevelSelectOk=1  D3DDepthFogOk=1
D3DTrilinearRaw=495        D3DTrilinearBlendOk=1  D3DVertexAlphaBlendOk=1
Tex4444Raw=992             Tex4444PixelOk=1       D3DContextCycleOk=1
TexFormatCount=2           BltFillPixelOk=1       FlipPixelOk=0 (known)
```

The whole-file diff is not empty this time, and what differs is worth naming
rather than hiding: the `Build=` stamp, six kernel heap addresses and the raw
dwords containing them, two texture handles, one HAL code pointer,
`VBlankStatus` (1 vs 0 — where the beam was when sampled), and three
millisecond timings. Nothing functional.

**On a family with no D3D engine**, measured on `Win98SE-Mach64VT2` with the
same build: `Stage=enable-ok`, and the DirectDraw probe reports
`D3DHalFound=0`, `TexFormatCount=0`, `D3DDeviceCount=3` — no hardware Direct3D
device offered — with DirectDraw itself fully working (`BltFillPixelOk=1`,
`SrcCopyPixelOk=1`, all four overlap cases, `RestoreHr=0x00000000`). That is
the case the broken version appeared to get right for the wrong reason, so it
was re-measured after the fix rather than carried over.

## What the split buys today, before any second engine

A second gate on a chip with no S3D core. One HAL binary carries every engine
and every family links it, so the ATI and VBE packages have always shipped the
ViRGE's D3D code and relied entirely on the 16-bit side nulling `lpD3D*` for a
chip whose `engine_caps` lack D3D. `v9x_d3d_engine()` now resolves null for
such a chip, every entry point declines, and `v9x_d3d_publish` writes nothing.
That is a second lock behind the first, not a replacement for it.

## Held by the gate, not by review

`check-tree.ps1` asserts both halves of the boundary, because this is exactly
the kind of property that decays by one convenient exception:

* `d3d_core.c` may not name `v9x_mmio_write`, `v9x_mmio_read`, `V9X_VIRGE_` or
  `V9X_TRIO_`;
* no `d3d_*.c` other than the core may define a `DWORD __stdcall V9x*` entry
  point.

The second rule is written over the directory rather than over a file list, so
the next engine is covered the day it is added.

## What this does not establish

That the boundary is right, and now also: that publish-time engine selection
works, because it does not exist. The binary publishes one engine's caps and
relies on the 16-bit clamp, exactly as before.

The rest stands. It is drawn around one engine, which is the
roadmap's own objection to doing this now, and the honest expectation is that
the 3dfx engine will want at least one thing this table does not offer —
`begin_batch`/`end_batch` around a FIFO, most likely, since the ViRGE needs no
such bracket and so none was invented. Adding it is a change to two files and
one call site. Moving `draw_triangles` off the triangle-list granularity would
not be, which is why that is the rule fixed hardest.
