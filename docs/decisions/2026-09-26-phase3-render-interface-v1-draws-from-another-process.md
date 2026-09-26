# Phase 3: render interface v1 is exported, reached from an application's own LoadLibrary, and draws exactly on the software engine

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 3 (and the Phase 0.9 question of
whether a call from the GL process reaches DirectDraw's HAL instance)
Guests: 86Box `Win98SE-Fast-D3D` (port 9878, vbe package, `Direct3D=2`
software), boot 596; 86Box `Win86SE` (port 9869, s3 package, ViRGE/DX),
boot 638. Both on packages built from the tree this record is committed
with, 1024x768x16 RGB565.
Evidence: `2026-09-26-phase3-render-interface-soft-V9XR3DP.ini`,
`2026-09-26-phase3-render-interface-virge-V9XR3DP.ini` (`V9XR3DP.EXE`,
`tools\diag\r3d_probe_win32.c`)

## Question

Can an application - standing in for the ICD - reach V9XHAL.DLL's
`V9xRenderInterface`, and does what it reaches share DirectDraw's HAL
instance (DriverInit run, `v9x_hal` set), rather than a private copy?

## Measured

- **The HAL is not in the application's module list** after
  `DirectDrawCreate` (`HalLoaded=0` on both guests). DirectDraw loads the
  32-bit HAL in its helper process; its shared-arena image is callable
  from everywhere without being a module of this one. Killed hypothesis:
  that `GetModuleHandleA("V9XHAL")` would find it, as the plan's Phase 3
  text assumed.
- **The application's own `LoadLibraryA("V9XHAL.DLL")` returns the shared
  image** (`HalBase=0xB0400000`, the link base) and the export is found.
  `describe` answers `OK` with generation 3 (software) and 2 (ViRGE):
  DriverInit's session counter, which a private copy would not have.
  This is the same instance.
- **Negotiation is exact:** version 2, and version 1 with a wrong size,
  both return null; version 1 with this header's size returns the table.
- **Software engine** (`DescribeEngine=1`, "Velocity9x Software", target
  formats `1 << RGB565`): clear to red and depth `0xFFFF` reads back
  `0xF800`/`0xFFFF`. A green quad at 0.5, a blue one at 0.75 over the left
  half and a blue one at 0.25 over the right half, then finish: left
  `0x07E0` at depth `0x8000` (the 0.75 quad rejected), right `0x001F` at
  `0x4000`. Exact. `DrawASubmitted=2`.
- **ViRGE** (`DescribeEngine=2`, "Velocity9x ViRGE"): on this RGB565
  desktop `DescribeTargetFormats=0`, and every draw is `UNSUPPORTED` (4)
  before anything reaches the S3D - the S3D writes 1555 into any 16-bit
  target (`2026-09-26-phase05-mixed-engine-ordering-and-virge-colour-
  mismatch.md`), and `accepts` refuses a 565 target. The target is left as
  cleared. Clear itself, a CPU fill after the shared drain, is exact.
- **Refusals, both guests:** a stale generation `STALE` (3), a request
  built for another size `ABI` (2), 65 triangles `INVALID` (1), a pointer
  that is not a surface `INVALID` (1), a CPU texture `UNSUPPORTED` (4); the
  target unchanged after all of them.

## What this does not establish

- Anything on Gen3: the netbook was offline for this run.
- Hold times under the Win16 mutex, and a GL client alongside a D3D one.
- A DirectDraw texture through the interface, a 555 desktop on the ViRGE,
  and any texture combine; the probe draws untextured.
- That the `IsBadReadPtr` guards catch a bad pointer inside a request's
  payload: the not-a-surface case is caught by the INT -> LCL guard, and
  no probe passes a bad vertex pointer yet.

## Decision

The ICD finds the HAL with `LoadLibraryA("V9XHAL.DLL")`, not
`GetModuleHandleA`, and must not `FreeLibrary` it below one reference
while DirectDraw holds it (it never will be the last holder while a
DirectDraw object exists). The plan's Phase 3 text is corrected to say so.
