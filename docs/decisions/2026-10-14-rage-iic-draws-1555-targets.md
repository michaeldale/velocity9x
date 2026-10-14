# The Rage IIC draws 1555 targets: Direct3D at 5:5:5 matches 5:6:5 but for one blend step

Date: 2026-10-14

Status: Measured on A8U4I5 (Rage IIC AGP), boots 461 to 463. Phase 0,
step 3 and the Rage II part of Phase 1 of
[ati-555-desktop.md](../plans/ati-555-desktop.md). Evidence:
`docs/probe/a8u4i5-rage-iic-555-2026-10-14/`.

## The change

- **`rage2_draw.c`.** The policy accepts an XRGB1555 target and records
  DP_PIX_WIDTH's destination and host fields for it: code 3, `0x00030003`.
  That is the code ATI's own driver ran the Rage IIC's 3D at (`0x40030203`
  under Half-Life, `hl-sample-ati/ATIRX-SAMPLE.TXT`). The state builder
  takes the value from the decision instead of the 565 constant. The tests
  were written first and failed five checks.
- **`d3d_core.c`.** Describe no longer withholds the desktop's layout from
  the Rage II engine.
- **`dd16.c`.** `v9x_dd_engine_wants_555` is true for the Rage IIC's
  engine, so it chooses 5:5:5 under hardware Direct3D. The Mach64 class
  is unchanged.

## The A/B

V9XDDP, the same build (`a8-555-2`) on the same card, compared by every
key ending `Ok`:
- **5:5:5** (boot 461, `HighColor=15`): 154 pass, target masks
  `7C00/03E0/001F`. The triangle reads `0x7C00`, exactly red.
- **5:6:5** (boot 462): 152 pass.

Four keys differ:

| Key | 5:5:5 | 5:6:5 | Reading |
|---|---|---|---|
| D3DBaseTextureOk | 1 (`03E0`) | 0 (`07C0`) | A 1555 texel into 565 is zero-extended (F1, 2026-10-02); into 555 it is exact |
| D3DTiledTextureOk | 1 | 0 | The same texel |
| D3DTiledNegativeOk | 1 | 0 | The same texel |
| BlendModulateOk | 0 (`03C0`) | 1 (`03E0`) | DESTCOLOR x white over green 31: one step low at 5:5:5 |

Every other check agrees, the texture matrix included.

The blend loses one least-significant step when it reads back a 5-bit
channel: green 31 of 31 comes back as 30. At 5:6:5 the same green,
31 of 63, comes back exact.
- **Not tested:** ATI's driver runs SCALE_3D_CNTL with dither and
  rounding on (low nibble `F`), and ROUND_EN may be the cure.
- **The keys that read 0 at both layouts** (the Z compares, the 2- and
  4-texel textures, clamp, bilinear and trilinear on one 1555 matrix row)
  are unchanged by this work.

## The default boot

- **Driver, boot 463, no `HighColor`:** `ColourLayout=555-auto`,
  `is555=1`, mode 0x116, `enable-ok`.
- **Cubes through ATI3DCIF.DLL:** the render interface describes an
  XRGB1555 target (`target formats=00000004`). Cubes draws into it: the
  target's centre reads `5000`, `5400` and `56B5` after its draws. Its
  clear value `4210` is now the grey it meant.

## Not done

- **Glide** refuses to open without a 565 target (plan, Phase 2).
- **Untagged texture surfaces** inherit the 5:5:5 display format, and
  the Rage II's texture classifier refuses XRGB1555 textures, so a
  Direct3D title that creates such textures will see them refused. None
  of the V9XDDP keys moved for it.
- **The 2D engine** still programs DP_PIX_WIDTH code 4 for fills and
  copies. Both are bit-copies, and the desktop drew correctly.
- **The Mach64 class.**

Gates: build-host, run-checks.
