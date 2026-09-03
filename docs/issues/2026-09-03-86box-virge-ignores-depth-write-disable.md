# 86Box's ViRGE writes depth with Z-write disabled; the Trio3D does not

Filed: 2026-09-03
Status: open, measured on the emulator and on silicon, not a driver defect
Component: 86Box `vid_s3_virge.c` (S3D depth path); instrument
`tools\diag\ddraw_probe_win32.c` ladder two

## What the probe asks

Ladder two of the depth test seeds the buffer at Z 0.25 (white, ALWAYS), draws
Z 0.125 in green with `ZWRITEENABLE = FALSE` under LESS, then draws Z 0.1875 in
red under LESS. The red draw is behind the green one and in front of the seed,
so it is accepted only if the masked green draw left the buffer at 0.25. The
final pixel must be red.

## What the two targets answer

Both runs on a 5:5:5 desktop under hardware Direct3D, so the colour channel
mismatch that hid this for a fortnight is out of the picture; raw values are
XRGB1555.

| Target | `D3DZNoWriteRaw` | `D3DZMaskRaw` | `D3DZWriteMaskOk` |
|---|---|---|---|
| 86Box ViRGE/DX, `Win86SE`, boot 530 | 992 (green) | **992 (green)** | 0 |
| A8U4I5, physical Trio3D/2X, boot 8 | 992 (green) | **31744 (red)** | 0 * |

\* The Trio3D row was read on a 5:6:5 desktop on 2026-09-02, so the probe's
derived expectation was `0xF800` and the key reads 0 for the colour reason
alone. Its raw value is the one that matters: red, in the engine's own layout,
meaning the masked draw did *not* update the buffer and the write mask works.

On the emulator the final pixel is still green: the masked draw wrote Z 0.125
regardless, and the red draw at 0.1875 was then rejected. Every other rung of
both ladders passes on the emulator once the desktop is 5:5:5 (see
`docs\decisions\2026-09-02-a-555-desktop-needs-three-places-to-agree.md`), so
this is the one depth behaviour on which the emulator and the silicon disagree.

## Why it is filed here and not against the driver

The driver programs the same command word on both targets - `v9x_d3d_z_compare`
and the `Z_UP_EN` bit are chip-neutral in `d3d_virge.c` - and the silicon does
what the word asks. A driver fix that made the emulator pass would have to do
something the silicon does not need, and would be tuning to the model.

What it does mean for this project: **`D3DZWriteMaskOk` cannot be used as a
pass/fail gate on the 86Box ViRGE**, and any Direct3D content that relies on
Z-write-disable - decals, transparent overlays drawn after opaque geometry -
will look wrong on the emulator and not on the card. A regression seen only on
86Box in that area is the emulator until the card says otherwise.

## Next

1. Confirm on A8U4I5 with a 5:5:5 desktop, where the key should read 1
   outright. The machine has been unreachable since 2026-09-02.
2. Read 86Box's S3D depth-update path against this: if `Z_UP_EN` is not
   consulted there, this is a one-line report upstream.
