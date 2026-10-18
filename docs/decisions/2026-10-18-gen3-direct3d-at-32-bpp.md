# Gen3 hardware Direct3D at 32 bpp: ARGB8888 with alpha writes off, 16-bit depth

2026-10-18, MICHAEL-NETBOOK (945GSE / GMA 950), boot 210, development
build `ac9d1ded-dirty` (this change). Phase 3, Gen3 row, of
[`../plans/32bpp-acceleration-and-3d.md`](../plans/32bpp-acceleration-and-3d.md).
Evidence: [`../probe/netbook-gen3-d3d-32bpp-2026-10-18/`](../probe/netbook-gen3-d3d-32bpp-2026-10-18/).

## Sources

- `COLR_BUF_ARGB8888` = `(3<<8)` and `DEPTH_FRMT_24_FIXED_8_OTHER` =
  `(2<<2)`: Mesa 20.3.5 `dri/i915/i915_reg.h:164-170`. xf86-video-intel
  2.99.917 uses ARGB8888 for a 32-bit destination (`uxa/i915_render.c:154`)
  and with D24 (`uxa/i915_video.c:155`).
- `S5_WRITEDISABLE_ALPHA` = `(1<<31)`: Mesa `dri/i915/intel_reg.h:140`, set
  from the colour mask at `i915_state.c:706`.
- Mesa picks the depth format from the depth buffer's pixel size alone
  (`i915_vtbl.c:611-616`), but its window configurations pair ARGB8888 only
  with D24S8 (`intel_screen.c:1056-1061`). So ARGB8888 with 16-bit depth was
  allowed by the encoding and not shown by any reference. That is the
  combination measured here.

## The change

The 16-bit driver compiles `i9xx_3d.c` and `i9xx_3d_decode.c` into I9XXCODE,
which stands at 57,305 of its 57,344-byte budget. So, following the
two-texture-unit precedent, everything new is compiled only where the model
is flat (`V9X_I9XX_TWO_UNITS`). I9XXCODE is still `0DFD9h` after this
change.

- `v9x_i9xx_retarget_state` rewrites a block the runtime or two-unit builder
  has just built. DST_BUF_VARS (always at dword 18) becomes ARGB8888 with
  16-bit depth, and S5 becomes `S5_WRITEDISABLE_ALPHA`. It refuses any other
  format and any block whose packets are not where the builders put them.
  Alpha writes are off so the X8R8G8B8 desktop's X byte keeps what the clear
  wrote, as the software engine does; fragment alpha is not stored.
- The flat decoder holds a runtime stream to an appended limits field,
  `target_format` (zero means 565 and D16, as before), and pins S5: zero, or
  alpha-write-disabled for ARGB8888. The 16-bit decoder never checked S5 and
  still does not.
- `v9x_d3d_i9xx_bind_target_at` measures a target's footprint at its own
  pixel size. The old form assumed two bytes and would have let a 32 bpp
  target's last row end past the aperture.
- `d3d_i9xx.c` opts in (`target_pitch_max_xrgb8888`, BUF_INFO's field) and
  reports DDBD_16 | DDBD_32 render depths; Z stays DDBD_16.

Host tests (build-host.ps1) failed first: the new test did not compile.

## Measured

`V9XD32` at 640x480, then V9XDDP and a trace snapshot, all on boot 210:

| Mode | HAL render depths | T1 flat (3861 judged) | T2 Gouraud corners | T3 depth | T4 offscreen | Result |
|---|---|---|---|---|---|---|
| 16 bpp | `0500h` | 0 mismatches, `12B3` | 3 of 3 | 3 of 3 | 0 mismatches | PASS |
| 32 bpp | `0500h` | 0 mismatches, `0013579B` | 3 of 3 (`00EA0B0B`, `0015DF0B`, `00150BDF`) | 3 of 3 | 0, `002468AC` | PASS |

- **The X byte is zero** in every judged 32 bpp pixel, so the alpha write
  disable took effect.
- **ARGB8888 with a 16-bit depth buffer orders depth correctly** (T3): the
  quad behind kept, the triangle in front drawn.
- The Gouraud corners differ from the software engine's by one level
  (`EA` against `E9`, `0B` against `0A`): the engine's own interpolation,
  within the probe's 200/60 bounds.
- Snapshot: `I9xxDrawsSubmitted=12` (six per run), `D3dContextRejects=0`,
  `I9xxRefuseLast=0`, no breadcrumb, idle or abandon counts.
- **V9XDDP at 16 bpp, the regression check:** all 78 `...Ok` keys are
  identical to the 0.19.0 release run on this netbook
  (`docs/probe/netbook-gma950-0.19.0-2026-10-18/V9XDD.INI`), including the
  19 that read 0 there.

## Not measured, and owed

- **24-bit depth.** `DEPTH_FRMT_24_FIXED_8_OTHER` is defined and refused by
  `v9x_i9xx_retarget_state` until it is measured. It also needs per-surface
  depth sizes in the core, a depth fill at four bytes, and the Z caps
  ([`../plans/intel-24-bit-depth.md`](../plans/intel-24-bit-depth.md)).
- Textures, blending, fog and the two-unit path at 32 bpp. The retarget
  covers both builders, but V9XD32 draws untextured only.
- A title at 32 bpp, and any timing.
- The OpenGL ICD at 32 bpp (Phase 4).
