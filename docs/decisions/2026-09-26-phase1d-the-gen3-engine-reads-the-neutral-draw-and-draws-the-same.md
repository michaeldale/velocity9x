# Phase 1d: the Gen3 engine reads the neutral draw, the netbook's probe reports the same, and its callbacks hold the Win16 mutex too

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 1d (third engine) and 1e
Machine: MICHAEL-NETBOOK (10.0.1.254:9869, agent `dev-inv2`), 945GSE /
GMA 950 rev 03, Windows 98 SE, 1024x576x16, boots 36 (before) and 37 (after)
Evidence: `2026-09-26-phase1d-gen3-V9XDD-before.ini` (the installed 0.8.1-era
driver set: HAL 109,568 bytes, before any Phase 1 change),
`2026-09-26-phase1d-gen3-V9XDD-after.ini` (intel-gma package at commit
b0fb7e4 plus this move: driver 104,932, HAL 111,616, mini-VDD 22,588),
`2026-09-26-phase1d-gen3-V9XSNA7.ini` (V9XTRACE on boot 37)

## What moved

`d3d_i9xx.c`'s entry is `v9x_d3d_i9xx_draw(const V9X_R3D_DRAW *, const
V9X_R3D_VERTEX *, DWORD)`. Fifty-four context reads became description
reads by the same field map the ViRGE move used - BUF_INFO and the clip
from `draw->target` and `draw->depth`, MAP_STATE and SAMPLER_STATE inputs
from `draw->texture`, the blend, alpha-test and depth-compare inputs from
their own fields - and the one lookup of the bound texture's surface reads
`draw->texture.object`. The mip-tree checks, the bind-map proof, the
allowlist decode and the ring submission are as they were. The stream
builders keep their `V9X_D3DTLVERTEX` field names behind one cast at the
entry. With this, every engine's `draw_triangles` is null and `draw` set,
which is Phase 1e; the core's fallback branch stays until the member is
retired.

## Measured

`V9XDDP.EXE` (855-line report, `Result=COMPLETE`, 3.7 s both times).
Before is the driver the netbook had been running since 0.8.1, so this
comparison spans the whole of Phase 1 for Gen3 - the moved clipper, the
per-batch description, and this engine's own move. Excluding the
per-process keys (timings, uptimes, the runtime's own pointers, the HAL's
texture-table addresses) the difference is **one key, `CbHalSurfFlags`**,
a code address inside the HAL that the probe echoes. Every result key is
the same: device flags 451, the pixel ladder, the render-target and Z
results, the texture matrix, colour key and blends.

The trace on boot 37 adds a measurement Phase 0.2 could not make on 86Box:
on physical hardware, with the Gen3 ring, DirectDraw 6.1a held the Win16
mutex at every callback sampled - Blt 18/18, Lock 1,113/1,113 at depth 2,
CreateSurface 64/64, DestroySurface 74/74, Flip 206,988/206,988,
DrawPrimitives 588/588 at depth 1 or 2, DrawOnePrimitive 4/4 - with
`Win16Resolved=0x80FF` and no clipped blit seen (`BltClipped=0`, the
probe has no windowed blit).

## Gates

- Host tests green; `run-checks` passed for all four families.
- Netbook, V9XDDP: as above.
- **Owed before the Phase 1 record closes:** 3DMark 99 at 1024x576
  (within noise of 643) and the 3D WinBench 98 quality suite on this
  machine, which earlier records ran with an operator at the keyboard;
  Final Reality on the 86Box ViRGE; the physical A8U4I5. Nothing in the
  tree launches those from the agent.

## Standing

Phase 1 is complete on the host and on the three probe gates (86Box
ViRGE, 86Box software, netbook Gen3); the benchmark gates wait on an
operator run. The netbook stays on this driver set. Phase 2 (growing the
shared core: rasterizer features, the shared drain, `accepts`) is next,
with Phase 0.5's mixed-engine probe at its start.
