# Engine-placed render targets are never freed

Date: 2026-11-15

Status: fixed 2026-11-23, measured on two engines (below). Found by
reading.

`v9x_d3d_place_release` (`src/display32/d3d/d3d_place.c`) frees a block
only for a surface whose caps include `MIPMAP`, `ZBUFFER` or `TEXTURE`.

Several engines' `create_surface` also place an offscreen render target
(`OFFSCREENPLAIN | 3DDEVICE`, not primary, not flip) with
`v9x_d3d_place_each`, because DirectDraw's heap gives it a pitch the
engine cannot draw to:
- `d3d_eg.c`, `d3d_r6.c`, `d3d_vi.c`, `d3d_nv50.c`, `d3d_r128.c` and
  `d3d_mga.c`, by the check they share;
- `d3d_rv280.c` from 2026-11-15.

Such a surface has `lpVidMemHeap` null, so DirectDraw does not free it
either. Each one leaks its block. The ICD makes one OpenGL back buffer
per drawable size, so a resized or recreated window leaks a back buffer
each time. That is the visible case.

A fix would add `3DDEVICE` to the caps `v9x_d3d_place_release` accepts.
The signature checks (block start in `dwReserved1`, no heap) already keep
a surface DirectDraw placed out of it. That fix is shared code for seven
engines, and none of them has been measured with it.

## Measured and fixed, 2026-11-23

A new probe, V9XRTLK.EXE (`tools/diag/rtleak_probe_win32.c`,
`scripts/build-rtleak-probe.ps1`), creates and releases one 640x480
`OFFSCREENPLAIN | 3DDEVICE` surface in video memory 20 times, then a
16-bit Z buffer 20 times as a control, reading DirectDraw 2's free
offscreen memory before and after.

Before the fix, on M-BXMFA8 (Oland 6611, `d3d_vi.c`), boot 186:
`Result=LEAK`. The free memory dropped 1,231,360 bytes with the first
target and did not come back on its release; 20 rounds lost 24,627,200
bytes. The Z buffers gave theirs back (`DepthLost=0`). That memory stayed
lost until the machine restarted.

The fix is the one above: `V9X_DDSCAPS_3DDEVICE` added to the caps
`v9x_d3d_place_release` accepts (`d3d_place.c`). After it, on the
`ati` set of `713bc38a-dirty` (release probes in
`docs/decisions/2026-11-23-1.0-b1-b3-b12-checks.md`):

| Machine | Engine | Target lost | Z lost |
|---|---|---|---|
| M-BXMFA8, Oland 6611, boot 187 | `d3d_vi.c` | 0 | 0 |
| M-D7TFW4, HD 7660D, boot 102 | `d3d_eg.c` | 0 | 0 |
| HD 4600, boot 298 (does not place targets) | `d3d_hsw.c` | 0 | 0 |

The HD 7660D was not run before the fix. `d3d_r6.c`, `d3d_nv50.c`,
`d3d_r128.c`, `d3d_mga.c` and `d3d_rv280.c` take the same path and were
not run. Evidence: `docs/probe/1.0-b1-b3-b12-2026-11-23/*-V9XRTLK*.INI`.
