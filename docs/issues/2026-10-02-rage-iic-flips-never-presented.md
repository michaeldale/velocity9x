# Rage IIC: page flips are declined and nothing presents them

Found 2026-10-02 on A8U4I5 (ATI 3D Rage IIC AGP, Velocity9x `ati`
package, desktop 1024x768x16), during the Phase 5 Direct3D gate
([first runs](../decisions/2026-10-02-rage-iic-direct3d-first-runs.md)).
Status: **fixed** in `1348660` (CRTC_OFF_PITCH written in the vertical
blank, [scanout start](../decisions/2026-10-03-rage-iic-scanout-start.md));
verified on boots 142-144 - every flip handled, none declined, Final
Reality and 3DMark 99 presenting on the monitor.

## What happens

A full-screen flipping application renders, and the screen never shows
it. Final Reality's standard run (boot 141) drew 559,842 batches and
1.64 M triangles on the engine with no timeout, while every agent
screenshot in its 3D tests was solid black at 640x480; `FlipHandled=0`,
`FlipDeclined=3667`. V9XDDP's `FlipPixelOk=0` (boots 140, 141): a flip
to a red back buffer leaves the screen as it was.

## Why

`v9x_flip_body` (src\display32\ddhal_core.c) declines a primary flip -
returns DDHAL_DRIVER_NOTHANDLED with DD_OK - when the engine stamps no
`V9X_DD_ENGINE_CAP_FLIP` (`v9x_can_set_display_start`). The Rage IIC's
stamp (`rage_iic_hw16.c`) carries fill, copy and, since `dbc0c2e`,
Direct3D; no flip. The comment there expects DirectDraw to present by its
own copy. On this machine it does not:

| Boot | HAL | Flip20Ms | FlipPixelOk |
|---|---|---|---|
| 126, 128 | refused (no HAL: DirectDraw emulates everything) | 505 | 1 |
| 140, 141 | attached | 0 | 0 |

So the defect dates from the HAL attaching on this chip (`fc1ed35`), not
from Direct3D; the first-bind record read the 0 ms as a speed-up.

## Not established

- Whether other HAL chips without the flip capability (the Mobility-M)
  are hit the same way.
- How the Rage IIC moves its scanout. In the VBE modes the Mach64 CRTC
  runs in extended mode, where the VGA start-address registers are
  likely ignored and CRTC_OFF_PITCH (survey: `CrtcOffsetBytes=0`,
  `CrtcPitchPixels=1024`) is the start. Unmeasured; and the agent's
  screenshots read the primary surface's memory through GDI, not the
  scanout, so verifying a start-address change needs someone looking at
  the monitor.

## Options

1. Implement the flip by CRTC_OFF_PITCH and stamp the flip capability,
   once a scene shows (on the monitor) that writing its offset moves the
   picture, and when it latches.
2. Without a flip capability, publish no Flip callback, so DirectDraw
   emulates as it did before the HAL attached. Shared code: a design
   change for every chip in that position.
