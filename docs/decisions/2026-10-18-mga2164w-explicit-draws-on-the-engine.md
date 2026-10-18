# MGA-2164W: explicit OpenGL and Glide draws on the engine where it is exact

2026-10-18, A8U4I5 (Matrox Millennium II, MGA-2164W, 8 MiB, 10.0.1.172),
boot 496, driver `50e7eeba-dirty`. Step 4 of
[`../plans/matrox-mga2164w-render-interface.md`](../plans/matrox-mga2164w-render-interface.md).
Evidence:
[`../probe/a8u4i5-mga2164w-render-interface-2026-10-18/`](../probe/a8u4i5-mga2164w-render-interface-2026-10-18/)
(`*-boot496*`).

## The change

`v9x_d3d_mga_map_draw` (`d3d_mga_map.c`, host-tested, the new cases
failing first) used to refuse every explicit draw. Now it maps them on
the terms MGA2D `/ri` measured
([2026-10-18-mga2164w-render-interface-registers.md](2026-10-18-mga2164w-render-interface-registers.md)).

On the engine:
- **Undithered.** `no_dither`, a new trapezoid field (`mga_3d.h`), sets
  MACCESS bit 30, host-tested in `test_mga_3d.c`.
- **The scissor as the clip window.** The half-open rectangle becomes the
  inclusive CXBNDRY, and YTOP and YBOT as `YDSTORG + row * PITCH`. An
  empty scissor draws nothing.
- **The write mask as PLNWT.** It is per channel by target layout (565,
  1555, 8888), replicated per 16-bit pixel.

Refused, and drawn by the software fallback:
- CPU texture levels and a second unit (`REFUSE_EXPLICIT`);
- any blend but none or ONE/ZERO (the stipple, the additive key and the
  multiply are Direct3D's approximations);
- a linear or mipmapped filter (the new `REFUSE_FILTER`);
- fog and specular, as before.

`d3d_mga.c` writes the four clip and plane-mask words before a batch that
narrows them, and the per-mode setup (`v9x_mga_build_setup`) after it, so
the 2D engine and GDI find the clip and mask open again. Direct3D draws
are unchanged: no `no_dither`, no state writes.

Also part of the change:
- MODULATE is allowed on explicit draws, as the plan's table said to
  decide by the probes. The probes below pass with it.
- The old host test that every explicit draw is refused was removed with
  the behaviour it held.

Gates: `build-host.ps1` and `run-checks.ps1` passed. The set was deployed
by WININIT rename, and the guest's V9XHAL.DLL hash matches the package's.

## Measured, boot 496, desktop 800x600x16

| Probe | Boot 495 (all through the fallback) | Boot 496 |
|---|---|---|
| V9XR3DP | `DrawOk`, `TexturedScissorOk`, `MaskedOk` 1 | the same; left `07E0`/`8000`, right `001F`/`4000`, masked `FFE0` |
| V9XGLP | `PASS`, every check 1 | `PASS`, every check 1 |
| V9XGLIDP (Glide 2) | `PASS`, 0 failures | `PASS`, 0 failures |

The trace after the three probes:
- 219 render-interface draws.
- 168 refused by the engine and drawn by the fallback:
  - 82 CPU levels or a second unit (`M64AcceptPolicy02`);
  - 75 linear or mipmapped filter (`14`);
  - 4 blend, 4 fog, 3 texture.
- So 51 were drawn on the engine.
- No idle timeouts.

Direct3D after the change, V9XD32 on the same boot:
- `/bpp16`: PASS, T1 `1AB3` (still dithered: Direct3D keeps the dither
  it was measured with), T7 average `007F7F00`.
- `/z24` at 32 bpp: PASS, T5 `00FF0000`.

The desktop was put back to 800x600x8 afterwards.

## Findings

- The 2164W draws the render interface's untextured, nearest-textured,
  scissored and masked draws itself, and the probes cannot tell those
  pixels from the software engine's.
- Most of what OpenGL sends in these probes is still drawn by the CPU.
  Filtering accounts for 75 refusals, CPU levels for 82. Decision 2 of the
  plan (whether explicit draws may take the Direct3D approximations, such
  as nearest for linear) needs a title's split, not a probe's: step 6.

## Not measured

- 32 bpp: V9XR3DP's oracle is 16-bit, and the ICD offers no 32-bit pixel
  formats.
- Whether MODULATE's darker rounding shows in a title.
- Speed. Nothing was timed.
