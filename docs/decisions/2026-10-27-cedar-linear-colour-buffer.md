# The HD 5450 renders to linear surfaces once CB_COLOR0_ATTRIB's NON_DISP_TILING_ORDER is set

Date: 2026-10-27

Status: **measured, one card, one boot.** M-BXMFA8's HD 5450 (`1002:68F9`,
Cedar), boot 7. It follows
[the first 3D draw](2026-10-27-cedar-first-3d-draw.md), where Linux 3.9's
textured copy was byte-exact on 1D-tiled surfaces and wrong on linear
ones. Tool: `V9XEG3D.EXE` with experiment switches (`/cbmode:`,
`/texmode:`, `/scissor:`, `/gbaddr:`, `/attrib:`). Captures: the
`V9XEG3D-linear-*` files in
[`../probe/m-bxmfa8-cedar-3d-2026-10-27/`](../probe/m-bxmfa8-cedar-3d-2026-10-27/).

## Finding the side at fault

With the scissor's right edge at 4 (`/scissor:4`), so only pixels 0 to 3
of each row were drawn, pixels 0 and 1 landed in dwords 0 and 1, and
pixels 2 and 3 in dwords 6 and 7. A pixel that is never drawn cannot
appear anywhere, so the texture fetch was not at fault: the colour buffer
put pixel x of each 8 at byte `4*x0 + 24*x1` and ignored x2. That is the
full run's pattern too, where pixels 4 to 7 then overwrite 0 to 3.

## Hypotheses tested

| Run | Change from the failing linear run | Result |
|---|---|---|
| a2 | both surfaces linear general (mode 0) | same failure |
| a3 | `GB_ADDR_CONFIG` = `02011003`, the BIOS's `HDP_ADDR_CONFIG` | same first row, more mismatches (448) |
| a4 | `GB_ADDR_CONFIG` = `02010000`, one pipe | same failure |
| b1, b2 | `CB_COLOR0_ATTRIB` = `10h` (`NON_DISP_TILING_ORDER`) | **byte-exact** |
| b3 | the same, both surfaces linear general | **byte-exact** |

The pipe configuration and the linear sub-mode are not the cause.
`NON_DISP_TILING_ORDER` is: Mesa's r600 driver sets it on every linear
colour buffer (`evergreen_set_color_surface_common`, read through the
Fuchsia Google Git mirror of Mesa). Linux 3.9's blit drew 1D-tiled
surfaces and left `CB_COLOR0_ATTRIB` at 0, so it never needed the bit.

## The default, and its control

V9XEG3D now sets the bit for linear colour buffers and leaves it clear
for tiled ones, as Mesa and Linux do:

| Run | Switches | Result |
|---|---|---|
| c1, c2 | none (linear aligned, bit set) | byte-exact |
| c3 | `/tiled` (bit clear) | byte-exact |
| c4 | `/attrib:0`, the bit forced clear on linear | the original failure, map identical |

V9XDDP on the DMA engine passed after these runs, on the same boot.

## What this settles

The 3D engine can render straight into linear surfaces, the kind
DirectDraw allocates, so Direct3D needs no tiled render targets and no
copy-out. Every linear colour buffer the driver programs sets
`NON_DISP_TILING_ORDER`.

## Not measured

- 16-bit and 8-bit colour buffers. Only `COLOR_8_8_8_8` was drawn.
- Pitches other than 64 pixels.
- Linear depth buffers. No depth buffer has been used yet.
