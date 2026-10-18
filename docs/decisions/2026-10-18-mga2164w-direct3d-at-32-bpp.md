# MGA-2164W Direct3D at 32 bpp, and 32-bit Z beside 16

2026-10-18, A8U4I5 (Matrox Millennium II, MGA-2164W, 8 MiB, 10.0.1.172),
boots 491 (before) and 492 (this change), driver `271a93bc-dirty`.
Phase 3 row 1 and decision 1 of
[`../plans/32bpp-acceleration-and-3d.md`](../plans/32bpp-acceleration-and-3d.md).
Evidence:
[`../probe/a8u4i5-mga2164w-32bpp-2026-10-18/`](../probe/a8u4i5-mga2164w-32bpp-2026-10-18/).

## Source

The register level was already measured by the MGA2D probe on this card
([2026-10-10-mga2164w-depth.md](2026-10-10-mga2164w-depth.md)):
- PWIDTH_32 trapezoids.
- 32-bit Z at ZWIDTH_32 in the DRn_Z32 LSB/MSB pairs (33.15).
- ZORG as the Z address less the target origin's pixel address times the
  Z cell.
- The compare is unsigned, and the stored value saturates at the top.

What was missing was the Direct3D path to them.

## The change

- **Map** (`d3d_mga_map.c`, host-tested, failed first):
  - XRGB8888 targets map to 4 bytes a pixel. The X byte is FCOL's top
    byte, which the map leaves zero.
  - The draw's Z cell picks DEPTH_16 or DEPTH_32. The Z pitch must be the
    target's pixel pitch at that cell, and ZORG takes the cell.
  - The core calls its 32-bit cell Z24X8; on this chip all 32 bits are
    depth.
- **Setup** (`mga_setup.c`, host-tested, failed first):
  - 32-bit depth was refused. It is now set up into the 33.15 pairs.
  - There is no 64-bit integer, so one magic-number addition rounds
    straight into both words.
  - A new test compares every stored Z against the exact plane at both
    widths. The worst is 0.50 units at 16 bits and 0.50 at 32.
- **Engine** (`d3d_mga.c`):
  - Opt-in: `target_pitch_max_xrgb8888` 8192 (2048 pixels) and
    `depth32_bits` 32.
  - Caps report DDBD_16 | DDBD_32 for render and Z.
  - Vertex z goes to the 32-bit scale in double, because a float holds 24
    bits. The 16-bit path is unchanged.
  - Targets are placed at the desktop's bytes, and Z buffers at their own
    cell.
- **dd16.c**: `dwZBufferBitDepths` gains DDBD_32 for the MGA engine type.
  Only the 2164W runs hardware Direct3D, which `deep_z` requires.
- **Core** (`d3d_core.c`, `d3d_state.c`):
  - Clear and Clear2 widened a 32-bit cell's value to 24 bits. They now
    widen it to the engine's `depth32_bits` with
    `v9x_d3d_state_depth_widen`, which is host-tested and gives the same
    result as before at 24.
  - A 32-bit Z buffer's pitch is bounded by the XRGB8888 pitch where an
    engine has one. On this chip, Z32 at 1280 pixels is 5120 bytes, past
    the 16 bpp bound of 4096.
- **2D fill** (`eng_mga.c`):
  - The fill draws at the mode's MACCESS pixel width. A depth cell of
    another size (Z16 on a 32 bpp desktop, Z32 on a 16 bpp one) is
    declined, and the CPU fills it.
  - Before this change, a Z32 fill on a 16 bpp desktop would have filled
    half the buffer. No such buffer could be made then.

## Measured

V9XD32 at 640x480. T5 is a quad a tenth of a 16-bit step nearer than
another.

| Boot | Run | HAL render, Z | T1 flat | T5 pixel | Result |
|---|---|---|---|---|---|
| 491 | 16 bpp, Z16 | `0400h`, `0400h` | `1AB3` | `07E0`, farther kept | PASS |
| 491 | 32 bpp, Z16 | `0400h`, `0400h` | - | - | `NO-HAL-AT-DEPTH` |
| 492 | 16 bpp, Z16 | `0500h`, `0500h` | `1AB3` | `07E0`, farther kept | PASS |
| 492 | 16 bpp, Z32 | `0500h`, `0500h` | `1AB3` | `F800`, nearer drawn | PASS |
| 492 | 32 bpp, Z16 | `0500h`, `0500h` | `0013579B` | `0000FF00`, farther kept | PASS |
| 492 | 32 bpp, Z32 | `0500h`, `0500h` | `0013579B` | `00FF0000`, nearer drawn | PASS |

In every boot 492 run:
- T2 (Gouraud corners), T3 (depth), T6 (the 1555 quadrant texture, DECAL)
  and T4 (offscreen target) pass.
- T7, the SRCALPHA blend drawn as the stipple, averages `007F7F00`.
- At 32 bpp the X byte of each pixel the probe reports (T1, T4, T5, T7)
  is zero.

The snapshot after the four boot 492 runs:
- 8 contexts and 44 batches through the engine sink;
- 4 Z surfaces placed, the last at `ZPlacedPitch=2560` (640 x 4: the
  16 bpp Z32 run), and 4 offered and accepted;
- every MGA refusal and setup counter zero, no idle timeouts.

`BuildsAgree=0` because the trace tool on the guest is the eb4bbd63
build; the DRV and HAL agree.

## Findings

- **The 2164W renders Direct3D into XRGB8888**, with the same results as
  at 16 bpp, untextured, textured and stippled.
- **It holds 32 bits of Direct3D depth** at either colour depth, and T5
  separates 32-bit from 16-bit Z at both.
- The Direct3D path agrees with what MGA2D measured at the register level
  on 2026-10-10.

## Found on the way, fixed after

The new depth-plane host test, with depths over the full range, found an
error that was already in the setup at 16 bits:
- A trapezoid whose first row is empty started its planes at that row's
  left column, outside the triangle.
- A depth start past the field's range was clamped, which offset every
  pixel after it. One steep triangle in 300 random ones was 420 units of
  16-bit Z off.
- A colour start past ±256 levels refused the whole triangle. Of 3000
  small triangles with red running 0 to 255, 774 were refused, and 161 of
  those had every step inside the field.

The first commit kept the test's depths in the middle half of the range.
The fix that followed it:
- `v9x_mga_setup_emit` starts a trapezoid at its first row that has a
  pixel. A trapezoid's width changes linearly, so empty rows can only be
  at its ends, and they draw nothing.
- A trapezoid with no pixel is left out.
- The depth test is back at the full range: 0 pixels off at both widths,
  worst 0.50 units, with the same 360,641 pixels drawn.
- The new colour test (`test_steep_colour`) now refuses 329 of the 3000
  triangles, each with a step outside the field. The other triangles that
  stopped being refused covered no pixel at all.
- The texture planes start from the same point. The perspective tests are
  unchanged: 0 refused, no texel off.

The fix has not been run on the card; it changes only which trapezoids
are sent and where their planes start.

## Not measured

- Which depth fills went to the engine and which to the CPU: the trace
  does not count the split. Z16 at 32 bpp and Z32 at 16 bpp are CPU fills
  by design, and T3 and T5 passing at those combinations shows that the
  clears landed.
- A title at 32 bpp on this card, and its frame rate with the CPU Z clear.
- 1280-wide 32 bpp modes; the probe runs at 640x480.
- The OpenGL ICD's depth stays 16-bit.
