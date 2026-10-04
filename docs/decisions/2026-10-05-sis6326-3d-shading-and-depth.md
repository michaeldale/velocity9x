# SiS 6326 3D: shading, Z16, alpha test and blending

Date: 2026-10-05. Machine: A8U4I5 boot 210, SiS 6326 card 2 (rev 0Bh),
Velocity9x `sis` at 800x600x16. Evidence:
`docs/probe/a8u4i5-sis6326-3d-phase2-2026-10-05/`. Plan:
[sis-6326-hardware-3d.md](../plans/sis-6326-hardware-3d.md), phase 2.
Follows [first triangle](2026-10-05-sis6326-3d-first-triangle.md).

## What was run

`SIS3D.EXE /phase2`, through `SIS2D.VXD` as in phase 1. The full state comes
from the new host-tested `v9x_sis3d_build_state`: ten dwords, enable first
and clip last. It covers Z16 set, Z base, alpha test, destination, fog,
blend and clip. Vertex floats come from `v9x_sis3d_float_fixed`, which
encodes any power-of-two fraction exactly.

Each scene:

1. cleared its target (and Z buffer, to FFFFh);
2. waited idle and wrote the state once;
3. fired its triangles one at a time, waiting idle before each;
4. read the target (and Z buffer) back.

The probe compared every pixel with a reference computed in integer
arithmetic: coverage at integer samples, the engine's bottom-right tie rule,
barycentric colour, the alpha and Z tests, then the blend. SR39 was restored
to 00h after every run. The additive scene ran last, because A8U4I5 has
locked on additive sprites under the Rage IIC. It did not lock here.

## Measured

**The vertex shift.** The phase 1 tie triangles were moved up and left by
one unit of 2^-4, 2^-8, 2^-10, 2^-12 and 2^-16 pixel. Each was compared
against Direct3D's top-left rule applied to the unmoved vertices:

| Shift | TieTopLeft (136 expected) | TieBottomRight (120 expected) |
|---|---|---|
| 1/16 | 136, match | 120, match |
| 1/256 | 136, match | 120, match |
| 1/1024 | 136, match | 120, match |
| 1/4096 | 136, match | 120, match |
| 1/65536 | 136, match | 120, match |

The setup engine resolves at least 2^-16 pixel at these coordinates (under
32 pixels). No tested shift was too small.

**Gouraud.** All three triangles had coverage identical to the reference:

| Scene | Colours | Pixels | More than one step off | Worst |
|---|---|---|---|---|
| Gouraud: red, green, blue corners | vary in x and y | 365 | 130 | 3 |
| GouraudX: red left, green right | vary in x only | 351 | 351 | 3 |
| GouraudY: red top, green bottom | vary in y only | 378 | 0 | 0 |

The error is horizontal only. Fitted offline from every pixel (file B):

| Model | GouraudX wrong pixels | Gouraud wrong pixels |
|---|---|---|
| colour at the sample (x, y) | 351 | 298 |
| colour at (x - 0.5, y) | - | 237 |
| colour at (x - 1, y) | **0** | 287 |
| span starts with the colour at the exact left-edge crossing, steps by dC/dx | **0** | **0** |

The x - 1 fit on GouraudX is an artefact: that triangle's left edge crosses
every row at an integer x, so the two models coincide there. The span model
fits both triangles exactly.

**Z16 with LESS.** Green was drawn at z 0.5, red at 0.25 overlapping it, a
blue triangle at 0.75 inside green, and a second blue at 0.75 clear of both.
Coverage and colour matched the reference exactly: red over green, the first
blue absent, the second present. The Z buffer read back:

| z | Z16 written |
|---|---|
| 0.25 | 2000h and 1FFFh |
| 0.5 | 4000h and 3FFFh |
| 0.75 | 6000h and 5FFFh |

Each triangle wrote two neighbouring values, though z was constant across
it.

**Z scale** (Z test ALWAYS, Z write on, one quadrant per z):

| z | Z16 written | File |
|---|---|---|
| 0 | 0000h | C |
| 0.99999994 (largest single below 1) | 7FFFh | D |
| 1.0 | **0000h** | C, D |
| 1.5 | 0000h | C, D |
| 1.99999988 | 0000h | C, D |

**Alpha test**, GREATER against 80h: the alpha-40h band was absent, the
80h band absent, and the C0h band drawn (116 pixels, exact).

**Blend.**

| Scene | Destination | Expected | Read back |
|---|---|---|---|
| SRCALPHA/INVSRCALPHA, red at alpha 80h | 001Fh blue | 800Fh | 8010h, all 374 pixels |
| ONE/ONE, C0C0C0h | 8410h grey | FFFFh if saturating | FFFFh, all 374 pixels |

## What this settles

1. **The driver's vertex shift is 1/256 pixel**, replacing phase 1's 1/16.
   The engine resolves 2^-16, so 1/256 has headroom. A single represents
   it exactly at every coordinate below 32768, which 2^-16 does not: an x
   of 800 keeps only 14 fraction bits. 1/256 is also sixteen times less
   geometric error than 1/16.
2. **Gouraud colour is prestepped in y but not in x.** Each span starts
   with the colour at the left edge's exact crossing of the row. The first
   pixel is up to one pixel's worth of dC/dx off, and the error carries
   along the span. This is the hardware's behaviour, and the driver
   accepts it. It cannot be corrected per vertex, because the offset
   differs from row to row.
3. **Z16 is z x 2^15, truncated, and wraps to 0 at z = 1.0.** Only the low
   15 bits of the 16-bit buffer are used. Direct3D allows z = 1.0 (the far
   plane), and on this engine it lands at the nearest depth. **The driver
   clamps z to 0.99999994 (3F7FFFFFh) before it reaches the engine.** A Z
   clear to depth d writes min(d x 2^15, 7FFFh): a fragment clamped to
   7FFFh then fails LESS against a 1.0 clear, as Direct3D requires.
4. **The compare codes work in the datasheet's order for Z (LESS) and
   alpha (GREATER, strict),** with the reference in 8A0Ch D[23:16].
5. **SRCALPHA/INVSRCALPHA blends within one step of the 8-bit reference.
   Additive ONE/ONE saturates per channel.** The blend nibbles are
   destination in D[31:28] and source in D[27:24], as the builder encodes
   them.
6. The full-state builder's encodings (8A00h-8A34h, ten dwords) are proven
   for every field the scenes used.

## Not established

- **The exact blend arithmetic.** At alpha 80h both channels came out at
  128 or above. That fits a/256 weights with rounding, among other
  formulas; one sample does not decide.
- **Whether texture coordinates and Z also skip the x prestep.** Z was
  constant across each triangle here, though it still wrote two
  neighbouring values. Phase 3 measures U/V, and a Z gradient belongs with
  it.
- **The shift at large coordinates.** Every target was 32x32, so no
  coordinate exceeded 32 pixels. The 2^-16 resolution is not measured at
  x = 800.
- **Why SiS's own Z tests fail** (88760231h). Point 3 is a candidate: a HAL
  that passes z = 1.0 through draws it nearest. It is untested against
  their driver.
- **The other compare codes and blend factors** (DST_ALPHA needs an A8
  buffer, which this target does not have). Phase 4's V9XDDP battery
  covers what applications use.
