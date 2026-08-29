# The Direct3D block splits into a chip-neutral core and one engine, and the probe cannot tell

Date: 2026-08-29
Branch: `d3d-core-engine-split`
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

## Measured: the probe cannot tell the difference

`V9XDDP.EXE`, 323 keys, run on the same guest before and after with only the
HAL rebuilt between:

```
Compare-Object before after  ->  (no differences)
```

Byte-identical. That includes every read-back pixel the probe renders, which
is what makes it evidence about behaviour rather than about linking:

```
D3DTrianglePixelRaw=31744   D3DTrianglePixelOk=1     D3DSubpixelTriangleOk=1
D3DBaseTextureRaw=992       D3DBaseTextureOk=1       D3DSpecularGouraudOk=1
D3DMipmapLevelRaw=31        D3DMipmapLevelSelectOk=1 D3DDepthFogOk=1
D3DTrilinearRaw=495         D3DTrilinearBlendOk=1    D3DVertexAlphaBlendOk=1
Tex4444Raw=992              Tex4444PixelOk=1         D3DContextCycleOk=1
Result=COMPLETE  D3DHalFound=1  D3DCreateDeviceHr=0x00000000  BltFillPixelOk=1
```

`FlipPixelOk=0` is the known, separately tracked result and is unchanged.

**The control that makes that meaningful.** An identical result is also what a
failed install produces, so two things were checked rather than assumed:

* the guest's `C:\WINDOWS\SYSTEM\V9XHAL.DLL` hashes
  `692C36CA95AA6D84270B90419A5F86F27AA7C78EC29FEA726D4372AFC6C108C6`, equal to
  the split build's staged DLL;
* the linked image carries a translation unit that did not exist before —
  `d3d_core.obj`, with `v9x_d3d_engine_` in it and `_v9x_d3d_engine_virge` as
  the engine's ops table.

So the guest ran the split binary and rendered the same pixels.

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

That the boundary is right. It is drawn around one engine, which is the
roadmap's own objection to doing this now, and the honest expectation is that
the 3dfx engine will want at least one thing this table does not offer —
`begin_batch`/`end_batch` around a FIFO, most likely, since the ViRGE needs no
such bracket and so none was invented. Adding it is a change to two files and
one call site. Moving `draw_triangles` off the triangle-list granularity would
not be, which is why that is the rule fixed hardest.
