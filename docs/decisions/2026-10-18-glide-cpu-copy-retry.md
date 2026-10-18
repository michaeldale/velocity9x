# Glide resends a refused surface texture as its CPU copy, as the ICD does

2026-10-18, A8U4I5 (Matrox Millennium II, MGA-2164W), boot 495. Step 3 of
[`../plans/matrox-mga2164w-render-interface.md`](../plans/matrox-mga2164w-render-interface.md).
Evidence:
[`../probe/a8u4i5-mga2164w-render-interface-2026-10-18/`](../probe/a8u4i5-mga2164w-render-interface-2026-10-18/)
(`V9XGLIDP-boot495-cpu-copy.INI`, `V9XGLIDE-boot495-cpu-copy.LOG`).

## Why

The Millennium II's software fallback
([2026-10-18-mga2164w-software-fallback.md](2026-10-18-mga2164w-software-fallback.md))
takes no surface texture: Gen3's mip trees are in a layout the software
sampler cannot read, so the core refuses every surface texture to the
fallback. The ICD already answers that UNSUPPORTED by sending its CPU copy.
Glide did not:
- the describe offers the 2164W surface textures, so Glide made them;
- the engine refused the explicit draw;
- the batch was lost.

At boot 495 that left V9XGLIDP with five failures, all of them textured.

## The change

- `v9x_glide_refusal_next` (`glide_state.c`, host-tested, failed first)
  says what a refused batch tries next, in the ICD's order:
  1. a surface texture's CPU copy;
  2. then, for a fogged batch, the first form without fog (an engine with
     neither fog nor a fallback, the ViRGE, refuses the copy too);
  3. then nothing.

  Before, the fog drop was the only retry.
- `glide_core.c`:
  - a batch remembers the texture source and the conversion it was bound
    with;
  - `v9x_glide_cpu_copy` converts that source to CPU texels on demand;
  - `v9x_glide_flush` takes the batch off the queue before drawing it,
    because the conversion flushes first and must find nothing held;
  - the census counts `cpu-copies`.

Gates: `build-host.ps1` and `run-checks.ps1` passed. GLIDE2X.DLL was put
beside the probe in `C:\V9XGLID`, with no driver change and no restart.

## Measured

| | Boot 495, before | Boot 495, this DLL |
|---|---|---|
| V9XGLIDP | `FAIL failures=5`: Texture565, TexturePalette, TexturePaletteChange, ChromaKeyed, ChromaOff read `0000` | `PASS failures=0`, all 21 checks |
| Glide census | - | drawn 29, refused 0, cpu-copies 10, fog-dropped 0 |

The ten CPU-copy triangles are the five textured quads.

## Not measured

- Glide 3. GLIDE3X.DLL has the same core, but V9XGL3P only opens and
  closes a session; no Glide 3 title was run.
- Engines other than the 2164W:
  - Gen3, Haswell, the VSA-100 and the Mach64 accept their surface
    textures, so the new path should not run there.
  - On the ViRGE and the SiS, a refused surface-texture batch now costs
    one more refused call and a texel conversion before the fog drop.
    That cost has not been measured on either chip.
- A Glide title on the 2164W.
