# DDI 8 and pixel shader 1.1 on the HD 4600: the record

Date: 2026-10-30. Step 7 of
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md),
the plan's closing record. Machine: the Dell i5-4570 (HD 4600,
8086:0412), Windows 98 SE, DirectX 9.0c, boots 107-166. Each step's own
record holds its logs; this one says what they add up to.

## The goal, and where it stands

3DMark 2001 SE's Game 4, Nature, runs on the HD 4600 and draws what it
should; its Pixel Shader test runs on the way.

- **It runs.** Nature, Pixel Shader and Advanced Pixel Shader complete,
  and the full default benchmark completes with every test: 1834 3D
  marks (boot 146; recorded, not compared). Boot 50 had Nature, Pixel
  Shader and Advanced Pixel Shader "not supported by hardware".
- **It draws what it should, except the distant terrain and mountains.**
  The lake, its reflections and ripples, the trees, grass, bridge and
  fisherman are drawn. The band behind the trees is white: 3DMark binds
  no texture to it, and why is parked (below).
- **Nothing is refused or dropped in 3DMark** (boots 146, 157): texture
  refusals 0, shader triangles dropped 0, PS kernels refused 0, no fault.

## What was built, step by step

| Step | Record | What it established |
|---|---|---|
| 1 | [`ddi8-on-win9x.md`](../specifications/ddi8-on-win9x.md) | DDI 8 is GetDriverInfo2 with `D3DCAPS8` and a format list; the records; PS 1.x semantics |
| 2 | [`…-step2.md`](2026-10-29-haswell-ddi8-step2.md) | DDI 8 without shaders; the runtime's surface halves go stale, so the driver keeps its own; 3DMark completes |
| 3 | [`…-step3-what-nature-asks.md`](2026-10-29-haswell-ddi8-step3-what-nature-asks.md) | Nature is gated on PS 1.1 and cube textures; the instructions it uses |
| 4 | [`…-step4-translator.md`](2026-10-30-haswell-ps11-step4-translator.md) | Parser, reference interpreter and Gen7.5 translator agree on the host |
| 5a | [`…-step5a-probe.md`](2026-10-30-haswell-ps11-step5a-probe.md) | Translated kernels run on the HD 4600 and match the interpreter |
| 5b | [`…-step5b-hal.md`](2026-10-30-haswell-ps11-step5b-hal.md) | PS 1.1 through Direct3D 8: V9XD32 T19; fog after the shader |
| 5c | [`…-step5c-cubes.md`](2026-10-30-haswell-ps11-step5c-cubes.md) | Cube textures placed and sampled |
| 5d | [`…-step5d-bump-formats.md`](2026-10-30-haswell-ps11-step5d-bump-formats.md) | Q8W8V8U8 and V8U8; Nature starts |
| 6 | [`…-step6-nature.md`](2026-10-30-haswell-ps11-step6-nature.md) | Four coordinate sets, an empty stage read as (0,0,0,1), shader triangles clipped; the white sky |
| — | [`…-dxt-textures.md`](2026-10-30-haswell-dxt-textures.md) | DXT1/3/5 placed and sampled |

## The premises

The plan's four recalled premises, and what the steps found:

1. **Nature needs PS 1.1 in hardware and accepts software vertex
   processing: held** (steps 3, 6). It also needed cube textures, signed
   bump formats, four coordinate sets and an empty stage that samples.
2. **Pixel shaders need DDI 8: held** (step 1), with a gate the plan did
   not know: the runtime asks GetDriverInfo2 only once
   `DDHALINFO_GETDRIVERINFO2` is set (step 2, boots 110-111).
3. **DDI 8 changes every DirectX 8 and 9 program's draw path: held**
   (step 2), so V9XD32, 3DMark and Half-Life were run on every build that
   touched the core.
4. **The GetDriverInfo2 requests and the format list: held** (step 1),
   with two additions found on hardware: the runtime describes Q8W8V8U8
   and DXT by FourCC with no bit count, so the engine places them (5d,
   DXT); and DirectDraw keeps a DXT surface's lPitch as its linear size,
   so a DXT level is handed to the CPU linear and written back into the
   block tree (DXT).

## The tolerance

PS draws are judged per pixel against the reference interpreter linked
into V9XD32 (T19): 9 per channel at R5G6B5 (one 5-bit step plus 2), 2 at
X8R8G8B8; host kernels against the interpreter within 2e-4. Every T19
program passed on boots 140, 145, 146, 156-157 and 162 at R5G6B5; the 32
bpp desktop was last judged at step 5c.

## What is refused, and why

- **Shader programs the parser does not accept** fail
  CreatePixelShader rather than draw wrong: texreg2ar, texreg2gb, the
  texm3x2 pair, texm3x3diff, anything from 1.2 on, and every register,
  mask or modifier rule 1.1 does not allow. The caps claim 1.1 only.
- **Cube and signed textures in a fixed-function draw:** the
  fixed-function kernels sample colours in two dimensions.
- **DXT cubes:** not published as cube formats; their layout is not
  measured.
- **A DXT texture DirectDraw placed** (when the engine's placement
  declined): laid out linearly, not in a block tree.
- **Shader vertices past the published ±8191 band** are dropped and
  counted; the runtime clips to the band, so none reached it in
  3DMark.
- **Points and lines under a pixel shader** are not drawn.

## Default and escape

DDI 8 with PS 1.1 is what DirectX 8 and 9 programs get on the HD 4600,
by default (no INI key or verb, `no-gated-defaults-dev-driver`). The
per-program `[Velocity9x.Direct3DDdi] <program>=7` in SYSTEM.INI is the
escape for a title that breaks, as it was for DDI 7. Half-Life Direct3D,
a DirectX 7 program, stays at DDI 7 and ran at every check (boot 146:
138.3 fps windowed, recorded).

## Open, parked, not checked

- **Nature's white sky**, parked with everything ruled out: DXT, texture
  size, formats, ops, address modes, projection, a depth-only pass, the
  texture-memory budget, render-to-texture. Next: a reference frame from
  real DirectX 8 hardware, or 3DMark under a debugger. The plan's Parked
  section has the detail.
- **Black Image Quality captures** after 3DMark's display dialog is used
  in a session; the frames are drawn, by the driver's counters.
- **Not judged by eye:** any 3DMark frame but Nature's Image Quality one;
  no frame was compared with a reference.
- **Not run:** T19's DXT, signed and cube programs at 32 bpp; DXT5 drawn;
  DXT mip chains judged; a second DirectX 8 title.
- **Parked from the start:** hardware vertex shaders, DOT3 and
  environment bump mapping as texture-stage ops, PS 1.2-1.4, and the
  other chips (Ivy Bridge, the HD 5450, Carrizo).
