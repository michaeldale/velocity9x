# MGA-2164W: the mixed frame passes, and refused render-interface draws go to the CPU

2026-10-18, A8U4I5 (Matrox Millennium II, MGA-2164W, 8 MiB, 10.0.1.172).
Step 2 of
[`../plans/matrox-mga2164w-render-interface.md`](../plans/matrox-mga2164w-render-interface.md).
Evidence:
[`../probe/a8u4i5-mga2164w-render-interface-2026-10-18/`](../probe/a8u4i5-mga2164w-render-interface-2026-10-18/).

## The gate: V9XDDP /mixed

This is the Phase 0.5 instrument
([2026-09-26-phase05-mixed-engine-ordering-and-virge-colour-mismatch.md](2026-09-26-phase05-mixed-engine-ordering-and-virge-colour-mismatch.md)):
1. The hardware draws red at depth 0.5.
2. The CPU locks the target and depth and stores RGB565 green and depth
   `4000h` at (16,16).
3. The hardware draws blue at depth 0.375, which must be rejected at
   (16,16) and accepted at (20,16).

V9XDDP sets 640x480x16 itself, and its oracle is RGB565.

Boot 493, driver `13812b70-dirty`, probe `ca5f4682`. The real run is in
`V9XDD2-mixed-boot493.INI`; the first file keeps the seeded zeros.

| Reading | MGA-2164W | Software reference (2026-09-26) |
|---|---|---|
| First hardware colour, depth | `F800`, `8000` | `F800`, `8000` |
| CPU colour, depth | `07E0`, `4000` | `07E0`, `4000` |
| Rejected pixel after the second draw | `07E0`, `4000` | the same |
| Accepted neighbour | `001F`, `6000` | `001F`, `6000` |
| `MixedOrderingOk`, `MixedColorEncodingOk`, `MixedDepthEncodingOk`, `MixedOk` | 1, 1, 1, 1 | 1, 1, 1, 1 |

The 2164W is the second engine to pass, after the software reference.
Its Z16 holds z x 65535 as the software engine's does: 0.5 gives `8000`
and 0.375 gives `6000`.

## The change

- `v9x_r3d_fallback` (`d3d_core.c`) returns the software engine for the
  MGA as well as Gen3.
- A draw with a 32-bit depth cell is not handed over, since the software
  engine's depth is 16-bit. Render-interface depth surfaces are made at
  the engine's 16 bits today, so the guard changes nothing yet; it states
  the rule.
- Surface textures stay refused, as on Gen3. The ICD then resends its CPU
  copy, which the fallback draws.

Gates: `build-host.ps1` and `run-checks.ps1` passed. The set was deployed
by WININIT rename, and boot 495 runs it: the guest's V9XHAL.DLL hash
matches the package's.

## Measured, boot 495, desktop 800x600x16

| Probe | Boot 493 (no fallback) | Boot 495 |
|---|---|---|
| V9XR3DP | every draw UNSUPPORTED, `DrawOk=0` | every draw OK; `DrawOk`, `TexturedScissorOk` and `MaskedOk` all 1 |
| V9XGLP | `PASS`, but 8 pixel checks 0 | `PASS`, every check 1 |
| V9XGLIDP (Glide 2) | `FAIL`, 14 failures | `FAIL`, 5 failures, all textured: `Texture565`, `TexturePalette`, `TexturePaletteChange`, `ChromaKeyed`, `ChromaOff` |

V9XGLIDP's untextured cases now all pass, including `BlendHalf` and
`FogTable`, which the CPU draws. The five textured ones read `0000`:
- Glide gives the 2164W surface textures, because the describe offers
  them.
- The engine refuses the explicit draw, and the fallback takes no
  surface texture.
- Glide, unlike the ICD, does not resend a CPU copy.

That is step 3 of the plan.

## Not measured: 32 bpp

- V9XDDP `/mixed` runs only at 640x480x16.
- V9XR3DP's oracle reads 16-bit words and packs 565 or 555. At
  800x600x32 its checks read 0 and mean nothing (its depth raws, `8000`
  and `4000`, are right).
- V9XGLP at 32 bpp chose "GDI Generic". The ICD offers no 32-bit pixel
  formats (Phase 4 of the 32 bpp plan), so it never reached the HAL.

The fallback is enabled at 32 bpp too: the software engine accepts
XRGB8888, and the ordering is the same drain. No probe has drawn a mixed
frame there.

The desktop was put back to 800x600x8 after the runs.
