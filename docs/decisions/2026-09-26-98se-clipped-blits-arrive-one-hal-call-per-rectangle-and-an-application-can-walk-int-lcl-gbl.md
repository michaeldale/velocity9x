# On 98SE a clipped windowed blit reaches the HAL as one call per visible rectangle with rDest already clipped, and an application can walk INT to LCL to GBL and find what the HAL sees

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phases 0.3 and 0.4
Guest: 86Box `Win86SE` (port 9869), Windows 98 SE, DDRAW.DLL 4.06.03.0518,
S3 ViRGE/DX, 1024x768x16, driver build `cb5e39a-dirty` (boot 633, shared
ABI 2026092603)
Instrument: `V9XSCLP.EXE` (`tools/diag/surface_clip_probe_win32.c`, built by
`scripts/build-surface-clip-probe.ps1`), and four counters appended to the
HAL's diagnostics that read the Win98 DDK's clipped-blit tail of
`DDHAL_BLTDATA` (`IsClipped`, `dwRectCnt`), which the HAL had never read
Evidence: `2026-09-26-98se-surface-clip-probe-9869.ini` (the probe),
`2026-09-26-98se-clipped-blt-V9XSNA7.ini` (the trace),
`2026-09-26-98se-clipped-blt-capture-9869.png` (the screen during the hold)

## Why

The OpenGL ICD presents a windowed frame the way GLDirect and Glide did: a
DirectDraw primary with a clipper on the window, and a `Blt` from a video
memory back buffer to the client rectangle. The HAL's `Blt` reads `rDest`
alone; if the runtime handed it one call with a rectangle list, the HAL
would draw over whatever covers the window. And the ICD hands the HAL a
surface's LCL found by casting the COM pointer, a cast documented for
drivers but not for applications. Both had to be measured before the plan
built on them.

## The geometry

Window A, a 400x300 popup at (100,100), owns the primary's clipper; a
400x300 video-memory back buffer is filled magenta and blitted into A's
client rectangle; window B, a 300x300 topmost popup at (300,250), covers
A's lower-right quarter. The probe holds for six seconds after the blit,
fills green and blits once more, and exits.

## Measured

**Phase 0.4, the walk from the COM pointer** (both surfaces, `Walk=ok`):

| | Back buffer | Primary |
|---|---|---|
| INT / LCL / GBL | `0x8322DFD8` / `0x8312D850` / `0x8312D904` | `0x8322DF60` / `0x8312CD04` / `0x8312CDB8` |
| `dwProcessId` = caller's `0xFFFD8E6F` | yes | yes |
| `fpVidMem` | `0xCC87F000` = `Lock`'s `lpSurface` | `0xCC6FF000` |
| `lPitch`, `wWidth`, `wHeight` | 800, 400, 300, as created | 2048, 1024, 768 |
| `ddsCaps` | `0x10004040` (offscreen, VRAM, local) | `0x1000C200` (primary) |
| `lpDDClipper` | 0 | `0x8322DF7C` (set) |

All three objects of both surfaces lie above 2 GB, and every field read
through the HAL's own `V9X_DD_SURFACE_LCL`/`_GBL` layouts is the value the
application put there or got back from `Lock`.

**Phase 0.3, the clipped blit.** Two application `Blt` calls to the primary
through the clipper (`ClippedBltHr=0`, `ClippedBlt2Hr=0`), and the HAL
counted six `Blt` callbacks in all: the two colour fills to the back
buffer, and **four** to the primary with `IsClipped=1` and `dwRectCnt=1`
each (`BltClipped=4`, `BltClippedRectsMax=1`, `BltClippedLastDest=0`, the
primary's offset). The visible part of A with B over its corner is an
L-shape, two rectangles; two blits, four calls. The capture shows A magenta
except under B, and B white and whole.

## What this settles

- **The runtime does the clipping.** DirectDraw 6.1a splits a clipped blit
  into one HAL call per visible rectangle, sets `IsClipped` and
  `dwRectCnt=1` on each, and puts the sub-rectangle in `rDest`. The HAL's
  `rDest`-only blit is correct for this runtime, and windowed Direct3D
  titles were never at risk from it. The counters stay, as the check for
  other runtimes.
- **The ICD's surface hand-off is sound on 98SE:** `INT->lpLcl->lpGbl`
  from an application yields the same LCL and GBL the HAL is given, the
  owner is recorded in `dwProcessId`, and `fpVidMem` is the `Lock` address.
  The plan's per-call validation in `r3d_validate.c` has the inputs it
  needs, and the ownership check it wanted is available.
- The `V9X_DD_SURFACE_LCL` layout the HAL placed by measurement on
  2026-09-03 is confirmed from the other side: `dwProcessId` at +24,
  `dwFlags` at +28, `ddsCaps` at +32, `lpDDClipper` at +40.

## What this does not establish

- Windows ME's DirectX 7 runtime or original 98's, which may clip
  differently.
- A blit whose clipped destination has more than two rectangles, or one
  with a source rectangle that the clipping must also cut: this probe used
  a full-surface source.
- Whether a Win16 GDI window moving *during* the blit is handled; the
  capture is one instant.

## Standing

Phases 0.3 and 0.4 are measured on 98SE. No HAL fix is needed for the
clipped blit; the plan's note that windowed D3D might be affected is
withdrawn for this runtime.
