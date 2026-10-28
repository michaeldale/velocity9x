# V9XDDP's zero rows on Carrizo: perspective was inverted, two features were missing, and a CPU Lock of the tiled Z buffer misaddresses it

Date: 2026-10-29

Status: **measured, one machine.** M-XP7053's Radeon R7E (`1002:9874`),
boot 34. The HAL was built on e7e3fbae plus this change; the DRV and VXD
are boot 22's. Captures are in
[`../probe/m-xp7053-carrizo-ddp-rows-2026-10-29/`](../probe/m-xp7053-carrizo-ddp-rows-2026-10-29/).

## The rows

[The fixed-function record](2026-10-28-carrizo-d3d-blend-and-shader-fixed-function.md)
listed the V9XDDP rows that read 0 at boots 26 to 33. Read against the
probe's source (`tools/diag/ddraw_probe_win32.c`), they fall into three
groups.

**Seeds, not results.** The probe writes these keys with
`DDERR_UNSUPPORTED` (`0x88760231`) and 65535 pixels before anything runs:
- the first `D3DZ` block, whose real results go to `V9XDD2.INI`, where
  they passed at boot 33;
- `D3DZP*`, written only under `/zprivate`;
- `Mixed*`, written only under `/mixed`.

Cedar's identical `0x88760231` values are the same seeds.

**Expected.** `MipTriDegradedOk` expects the degraded, unblended result, so
it reads 0 beside `D3DTrilinearBlendOk=1`. `FlipPixelOk` reads which page
GDI owns, so it is no verdict with real flips (`settings_status.c`).

**Real.** At boot 33:

| Row | Raw | Expected |
| --- | --- | --- |
| `Tex8X8888HalvesOk` | left and right both 0 | green, then blue |
| `Tex8DecalAlpha4444Ok` | 0 | red and green both near half |
| `Tex8DecalAlpha565Ok` | 0 | pure green |
| `PerspOk` | row 10 blue from x≈16; mid 31 | blue from x≈45; mid green |

## Causes

- **XRGB8888 textures were refused.** `v9x_d3d_vi_format_of` matched only
  the four formats with alpha or 565, so the draw was skipped.
- **DECALALPHA was refused.** No PS was built for it.
- **Perspective was inverted.** `PA_CL_VTE_CNTL` had `VTX_W0_FMT` set
  (`0x700`). With it set the PA weights the barycentrics by the inverse of
  what Direct3D's rhw means. Correct interpolation reaches u = 0.5 at 80%
  of the probe's edge and affine at 50%; boot 33 reached it at about 20%,
  which is the weights 1 and 4 in place of 1 and 0.25. Cedar measured the
  same bit on the same PA design (`eg_3d.c`, `EG_VTE_XY_Z_DIVIDED`, M-BXMFA8
  2026-10-27). The uniform-rhw rows (`PerspW*`) passed throughout, because
  a uniform weight divides out.

  3DMark 2000's frames at boot 33 did not show the fault in still
  screenshots.

## Changes

- `VI_VTE_SCREEN_SPACE` is now `0x300`, with VTX_W0_FMT clear.
- XRGB8888 is matched and offered as a fifth texture format. It samples
  as 8_8_8_8 with W selected as `SQ_SEL_1`, so its top byte is never read
  as alpha (`texture_opaque` in `v9x_vi_tl_draw`).
- DECALALPHA builds a PS:
  - parameter 0 is interpolated into v8-v11, as for modulate;
  - then each of B, G and R takes `v_subrev_f32 v16, diffuse, texel` and
    `v_mac_f32 diffuse, v16, v7` (the texel's alpha);
  - the diffuse alpha is kept.

  A texture without alpha samples W as 1, so it draws the texel. The cap
  `D3DPTBLENDCAPS_DECALALPHA` is reported.

## Boot 34

- V9XDDP: all four rows read 1.
  - `Tex8X8888` reads 2016 and 31.
  - `Tex8DecalAlpha4444Raw=29760`, which is R 14 and G 34 of 31 and 63.
  - `Tex8DecalAlpha565Raw=2016`.
  - Perspective row 10 turns blue between x 46 and 50.

  No other `*Ok` key changed from boot 33.
- V9XD32 PASS at 32 and 16 bpp.
- `/zprivate`: the private-device Z ladder passes on a hardware device
  (`D3DZPIsHardware=1`, compare and write mask OK).
- 3DMark 2000's default benchmark ran to 2,994. Game 1 and Game 2 frames
  draw correctly (`boot34-3dmark/`).

## Open: `/mixed`, a CPU Lock of the Z buffer

`/mixed` has a hardware draw, a CPU write and a second hardware draw
meet on one Z16 buffer. Colour is correct: `MixedColorEncodingOk=1`, and
the CPU's green pixel reads back. Depth is not:
- After the hardware wrote 0.5, the CPU read `0xFFFF` at (16,16)
  (`MixedFirstDepthRaw=65535`).
- The CPU then stored `0x4000` there. The second draw, at 0.375, should
  have been rejected at that pixel, but it was accepted and drew blue
  (`MixedRejectColorRaw=31`).
- At (20,16) the CPU read `0xFFFF` where the hardware had written 0.375.

Both sides address the same pixel at different bytes. The DB writes the
buffer 1D-tiled in 8x8 micro tiles (tile index 5,
[the depth record](2026-10-28-carrizo-depth.md)), and a Lock gives the CPU
a linear view of the same memory.

Every engine that passes `/mixed` has a linear Z buffer: Millennium II,
Mystique and Rage 128. None with a tiled Z has run it. Two ways out,
neither measured:
- a linear-aligned Z buffer, if GFX8's DB accepts one (tile index 8);
- detiling into a linear shadow on Lock and retiling on Unlock.

A game that locks its Z buffer would see the same misaddressing. None of
those run so far is known to.
