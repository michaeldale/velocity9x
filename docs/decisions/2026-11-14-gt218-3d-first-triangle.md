# GT218 Tesla 3D: the first triangle draws, exact at 32 and 16 bpp

Date: 2026-11-14

Phase 2 of [docs/plans/nvidia-gt218-3d.md](../plans/nvidia-gt218-3d.md),
on M-BXMFA8 (GeForce 210, GT218 A8), boot 117. Evidence:
`build/driver-results/m-bxmfa8-gt218-3d/p2-V9XN5CH.INI` and
`p2-exec.txt`.

## What ran

`V9XN5CH /lean /3d /3d:draw`, built at f26b3d69 (the report's build id).
On the probe's own channel, after the 2D stages and phase 1's bind and
setup (which passed again, with the same last-method words), one solid
rectangle at each of two depths:

- a 128x72 pitch-linear surface filled with `5A5A5A5Ah` by the CPU;
- EXA's solid picture written beside the programs: the texel
  `FFA5C3E1h`, TIC entry 0 (`2A712488h`, the texel address, then EXA's
  words), TSC entry 0, and identity vertex constants for both units;
- one push: render target 0 linear (`RT_HORIZ_LINEAR | pitch`, tile mode
  0, `RT_ARRAY_MODE` 0), blending off, `FP_START_ID` 0 (`PFP_S`), TIC
  flush and bind, the scissor at (16, 8) to (56, 32), and EXA's one
  triangle twice that size, three inline vertices;
- every pixel read back through BAR1.

## Result

| | Inside (40x24) | Outside | Pusher, PGRAPH |
| --- | --- | --- | --- |
| 32 bpp, `BGRX8` (E6h) | 960 of 960 `A5C3E1h` | 8256 of 8256 pattern | clean, idle, no interrupt |
| 16 bpp, `B5G6R5` (E8h) | 960 of 960 `A61Ch` | 8256 of 8256 pattern | clean, idle, no interrupt |

**The 3D engine draws.** At both depths the rectangle is exactly where
it was asked to be, every pixel of it is one value, and not one pixel
outside it changed. The vertex program ran (the positions are right),
the fragment program ran and sampled the texture (the colour is the
texel's), the TIC and TSC were read from where the setup said, and a
pitch-linear render target works with `RT_ARRAY_MODE` 0. No
`DMA_NOTIFY` was needed. PGRAPH's last method after each draw is
`VERTEX_END_GL` (15E0h) on subchannel 1, class 8597h.

The probe reported 16 bpp as `PIXELS-DIFFER` because it expected
`A61Bh`. That expectation was wrong, not the draw: the method list
assumed a x31 conversion (E1h to 27.35, which rounds and truncates to
27). The card wrote blue 28 (1Ch). All three channels fit **keeping each
channel's top bits**: A5h >> 3 = 20, C3h >> 2 = 48, E1h >> 3 = 28, the
same as floor(value x 32 / 255) for every 8-bit value but 255. One colour
does not prove a rule. A colour whose channels separate truncation from
rounding at 32 is the check if it matters, and it will matter for
Direct3D's 16 bpp output. The probe now expects `A61Ch`, and has not been
rerun with that expectation.

## What this does not cover

- Gouraud shading, real textures, depth and blending: phase 3.
- The x31 vs x32 question beyond this one colour.
- A Z buffer, which a linear target cannot have
  ([method list](../specifications/gt218-tesla-3d-first-triangle.md)).
- The HAL: nothing here is in V9XHAL.DLL yet.

The machine stays up on the merged nvidia build (2c0f02e8), boot 117.
