# Haswell hardware Direct3D at 32 bpp: B8G8R8A8 with alpha writes off

2026-10-18, the Dell i5-4570 (HD 4600, `8086:0412`, 10.0.1.254), boots 29
(before) and 30 (this change), driver `c57bcede-dirty`. Phase 3, Gen7.5
row, of
[`../plans/32bpp-acceleration-and-3d.md`](../plans/32bpp-acceleration-and-3d.md).
Evidence:
[`../probe/hsw-4570-d3d-32bpp-2026-10-18/`](../probe/hsw-4570-d3d-32bpp-2026-10-18/).

## The change

Haswell's draws took 565 or 1555 targets and refused anything else.
`v9x_d3d_hsw_target` (`d3d_hsw_map.c`, host-tested, failed first) now maps
XRGB8888 to `B8G8R8A8_UNORM`. That is the render-target format the Phase D
and E bring-up scenes drew on this chip (`v9x_intel_hsw_shade_state`). It
adds BLEND_STATE's alpha write disable, ORed into the draw's own mask, so
the X byte keeps what the clear wrote, as the Gen3 and software engines
do. `B8G8R8X8_UNORM` was not used: whether Gen7.5 renders it was not
established from the PRM tables.

The engine opts in with `target_pitch_max_xrgb8888` (16384, its 16 bpp
bound) and reports DDBD_16 | DDBD_32 render depths. Depth stays D16
(Y-tiled, as F2b built it).

## Measured

`V9XD32` (T1 flat, T2 Gouraud, T3 depth, T4 offscreen target, T5 16-bit
precision, T6 texture, T7 blend), 640x480:

| Boot | Driver | Mode | HAL render depths | Result |
|---|---|---|---|---|
| 29 | 0.19.x (installed) | 16 bpp | `0400h` | PASS, all seven |
| 29 | 0.19.x | 32 bpp | `0400h` | `NO-HAL-AT-DEPTH` |
| 30 | this change | 16 bpp | `0500h` | PASS, all seven |
| 30 | this change | 32 bpp | `0500h` | **PASS, all seven** |

At 32 bpp:
- T1 flat is `0013579B`, exact, with the X byte zero over 3,861 judged
  pixels.
- T2's corners and centre (`00EA0B0B`, `00555555`) match the Gen3 engine's
  exactly.
- T6's texels land at full scale (`00FF0000`, `0000FF00`, `000000FF`,
  `00FFFFFF`), and T7 blends to `00807F00`.

Snapshot after both boot 30 runs:
- 4 contexts, no context rejects;
- every Haswell refusal counter (`M64PolicyNN`) at zero;
- `R3dListSinkBatches=22` (eleven draws a run) and `DrawsNoHandle=20`
  (the ten untextured ones a run);
- no breadcrumb or idle timeouts; DRV, HAL and trace builds agree.

**Regression, render interface at 16 bpp:** V9XR3DP, V9XGLP and V9XGLIDP
on boot 30 against the boot 29 package-install run
(`../probe/hsw-4570-package-install-2026-10-18/`). They are identical
apart from a describe generation counter, the free-VRAM figure and a
process id: V9XGLP PASS, V9XGLIDP PASS with 0 failures, V9XR3DP COMPLETE.

## Not measured

- 24-bit depth on Haswell (D24_UNORM_X8 or D32_FLOAT): not built; Z16 at
  both depths.
- A title at 32 bpp on Haswell, and the OpenGL ICD at 32 bpp (Phase 4).
- B8G8R8X8_UNORM as a render target.
