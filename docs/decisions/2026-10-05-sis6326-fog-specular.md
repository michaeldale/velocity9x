# SiS 6326 vertex fog and specular, and V9XDDP against SiS's HAL

Date: 2026-10-05. Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), boots 230
and 231. Evidence: `docs/probe/a8u4i5-sis6326-fog-specular-2026-10-05/`.
Plan: [sis-6326-hardware-3d.md](../plans/sis-6326-hardware-3d.md), phase 4.
Follows [the Direct3D engine](2026-10-05-sis6326-d3d-engine.md),
[15-bit Z](2026-10-05-sis6326-z-compares-15-bits.md) and
[page flips](2026-10-05-sis6326-page-flips.md).

## What was built

- **The builder** writes 8A20h: D24 set (normal fog, read as per-vertex)
  and D[23:0] the fog colour, while the enable word carries fog (D3).
  Otherwise it writes 0, as before. Host-tested.
- **The mapping** turns Direct3D's FOGENABLE into enable D3 plus the fog
  colour. SPECULARENABLE becomes enable D4, set only where some vertex
  carries specular colour. Each vertex's specular dword, with the fog
  factor in its alpha byte, already goes to the vertex fog/specular
  register unchanged. Host-tested.

## Measured: SIS3D /phase5 (boot 230)

Nine draws through the driver's own mapping, every one on the textured
path (an untextured draw samples the Cpix dummy), each read at one pixel
against Direct3D's definitions:

- specular: colour = saturate(blended colour + specular RGB);
- fog: f x colour + (1 - f) x fog colour, with f the specular alpha / 255,
  applied after specular.

| Case | Read | Expected | |
|---|---|---|---|
| Spec: diffuse 404040 + specular 2080FF | 661F | 661F | match |
| SpecSat: C0C0C0 + 808080 | FFFF | FFFF | match |
| FogHalf: red, f 80h, blue fog | 8010 | 800F | one step |
| FogZero: f 00h | 001F | 001F | match |
| FogFull: f FFh | F800 | F800 | match |
| TexSpec: green texel COPY + specular 200020 | 27E4 | 27E4 | match |
| TexModSpec: green texel x 808080 + specular 200020 | 23E4 | 2404 | one step (MODULATE's /256) |
| TexFog: green texel, f 40h, red fog | BA00 | BA00 | match |
| SpecFog: 400000 + specular 004000, f 80h, blue fog | 2110 | 210F | one step, so specular comes before fog |

## Measured: V9XDDP (boot 231)

V9XDDP completed, and the snapshot read 0 idle timeouts and 0 declined
flips, with 5 batches refused (the DESTCOLOR draws).

- **`D3DSpecularGouraudOk`, `D3DFogTexOk`, `D3DSpecularTexOk` and
  `D3DDepthFogOk` all pass.** The fog ramp read 07E0, 0410 and 001F at
  factors 255, 128 and 0.
- **Against SiS's own HAL** (`V9XDD-CARD2-SIS228-B207.INI`, the same card):
  - 103 checks pass on both;
  - **none** pass under SiS's HAL and fail here;
  - `PerspOk`, `MipLadderShapeOk` and `TexMatrixCountsOk` pass here and
    fail there.
- **The step log:** 202 Ok keys pass and 19 fail. Each failure is
  explained:
  - The first-stage `D3DZ*` keys hold V9XDDP's not-run marker; the
    second stage passes.
  - `FlipPixelOk` is the GDI-page blind spot (page-flip record).
  - `MipTriDegradedOk` and the `Mixed*` keys fail under SiS's HAL too.
  - `BlendMultiplyOk` uses DESTCOLOR, which is refused as unmeasured.
  - `ZDepthFillOk` compares a raw readback the 15-bit depth scale changes
    (15-bit record), and `SpriteOk`, the `Spr_zon_*` cells and `RampOk`
    depend on that check's second fill.

## What this settles

1. **Vertex fog and specular are Direct3D's** on this engine, flat and
   Gouraud, textured and not, with specular before fog. The driver
   publishes FOGVERTEX, FOGFLAT, FOGGOURAUD, SPECULARFLATRGB and
   SPECULARGOURAUDRGB.
2. **Phase 4's exit gate is met.** The plan asked for V9XDDP's triangle,
   shading and texture battery to match SiS's HAL on the same card. It
   matches, with three more passes.

## Not established

- **Table fog** (D3DRENDERSTATE_FOGTABLEMODE). The core passes vertex fog
  only, and 8A20h's constant mode (D24 = 0) was not tried.
- **The colour factors (SRCCOLOR, DESTCOLOR) and colour keys.** Both are
  still refused, and neither is measured.
- **Applications.** Phase 5 of the plan (Final Reality, Half-Life,
  3DMark 99) has not run.
