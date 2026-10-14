# The Rage IIC blends one step low into a 5:5:5 target

Date: 2026-10-14

Status: Open, parked by Michael on 2026-10-14 to come back to later.
Found on A8U4I5's Rage IIC AGP; the Rage XL does not show it.

## What is seen

V9XDDP's BlendModulate draws DESTCOLOR x white over green 31. On the Rage
IIC with the same build (`a8-555-2`):
- **5:6:5** (boot 462): BlendModulateOk=1. Green 31 of 63 reads back
  exact, `03E0`.
- **5:5:5** (boot 461): BlendModulateOk=0. Green 31 of 31 reads back as
  30, `03C0`.

When the blend reads a 5-bit channel back, it loses one least significant
step. Every other key agrees between the two layouts.
Evidence: `docs/probe/a8u4i5-rage-iic-555-2026-10-14/boot461-555-V9XDD*.INI`,
`boot462-565-V9XDD*.INI`, and
[2026-10-14-rage-iic-draws-1555-targets.md](../decisions/2026-10-14-rage-iic-draws-1555-targets.md).

On the Rage XL (Rage Pro class), BlendModulate passes at both layouts
(boots 467 and 469,
[2026-10-14-rage-xl-555-desktop.md](../decisions/2026-10-14-rage-xl-555-desktop.md)).

## Hypothesis, not measured

ATI's driver runs the Rage IIC's SCALE_3D_CNTL with its low nibble at `F`
(`docs/probe/a8u4i5-rage-iic-registers-2026-10-02/hl-sample-ati/ATIRX-SAMPLE.TXT`).
Velocity9x leaves those bits clear. They plausibly hold dither and
rounding enables (a ROUND_EN or similar), and rounding would cure a
truncated LSB. The tree defines none of these bits for the Rage II, and
no databook naming them has been read.

## Next step

Add a scene to the ATIRX tool (`tools/diag/ati_rage2_scene_win32.c`) that
draws the BlendModulate case into a 1555 target, sweeping SCALE_3D_CNTL
bits 0-3 one at a time and together, and reads each result back. Record
which bit, if any, gives `03E0`, and what it does to the other V9XDDP
keys, before `rage2_draw.c` sets it.
