# Gen3 DirectDraw blits on the engine: Half-Life's present from 32 ms to 0.6 ms

2026-09-25, MICHAEL-NETBOOK (945GSE / GMA 950), Win98 SE. Half-Life GOTY
at 640x480x16, Direct3D renderer. Before: boot 27, the 3385c4d HAL
(106,496 bytes). After: boot 28, the same tree plus `eng_i9xx.c` and
`i9xx_blt.c` (V9XHAL.DLL 109,056 bytes), swapped in alone by WININIT.INI
rename; the shared ABI stamp (2026092505) did not move. Authorised by
`2026-09-25-intel-2d-blits-errata-gate.md`.

## Why

Boot 27 showed Half-Life presenting every frame by Blt from a back buffer
at 0x96000 to the primary at 0, with no flips after the first 720 of the
boot, and every Blt completed by the CPU through the aperture
(`CountBltEngine` absent, i.e. zero).

## Measured (`...-halflife-cpu-blits-V9XTRACE.ini`, `...-engine-blits-V9XTRACE.ini`)

TSC cycles per call, from `TimeBlt*`; the netbook's TSC runs at about
1.63 GHz (TSC delta against tick delta in the same file).

| | boot 27, CPU | boot 28, engine |
|---|---|---|
| Copy blits | 11,237 | 2,932 |
| Cycles per copy | 53.2 M (~33 ms) | 0.95 M (~0.6 ms) |
| Fill blits | 11,209 | 2,902 |
| Cycles per fill | 10.3 M (~6.3 ms) | 0.46 M (~0.3 ms) |
| Blits on the engine | 0 | 5,834 of 5,834 |
| Copies per second of D3D uptime | ~12.6 | ~25.4 |

The last row is copies over the seconds since `UptimeFirstD3d`, with menus
and loading included and different scenes played; it is an indication,
not a frame rate. Both per-call figures include the wait for the
breadcrumb, which on the engine path is the blit itself completing.

Nothing regressed that is counted: `EngineResets`, `BreadcrumbTimeouts`,
`BreadcrumbAbandoned`, `RenderDrainStalls` and `I9xxDrawsRefused` all
zero, 1,696,728 breadcrumbs landed. No blit was declined to the CPU, so
the overlap refusal was not exercised.

The operator: "working faster and looking pretty good"
(`...-engine-blits-panel.jpg`).

## Not settled

- **The gaps between the rails draw black** where they should be see-through
  (the panel photograph). Not a blit: the rails are a 3D draw. Counters in
  the same file: `AlphaTestSets=23662`, `AlphaTestUnexpressed=0`,
  `D3dColorKeySets=0`, and `D3dTextureRefusedShape=101707` of 1,690,894
  draws, the last refused 4 texels wide. Whether the rail's texture is one
  of the refused (drawn untextured) or is bound and its transparent texels
  survive the alpha test is not established.
- Erratum 12's boundary. This run did not provoke it, which says nothing
  about a heavier one.
- Overlapping copies (build 003 of the plan) still go to the CPU.
