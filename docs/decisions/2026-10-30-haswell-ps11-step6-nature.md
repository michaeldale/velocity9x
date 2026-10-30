# Nature on the HD 4600: what its frame showed, and three fixes

Date: 2026-10-30. Step 6 of
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md).
Machine: the Dell i5-4570 (HD 4600), boots 134-146, HAL-only swaps on
eb9cbcd4. Evidence:
[`../probe/hsw-4570-step6-nature-2026-10-30/`](../probe/hsw-4570-step6-nature-2026-10-30/).

## Getting a frame

The agent's screenshots miss fullscreen flips. 3DMark 2001 SE's Pro test
"Image Quality - Game 4" renders one Nature frame and writes it to
`C:\WINDOWS\TEMP\Quality\IQ04I_LastI.bmp` (1024 x 832: a 64-row caption
over the 1024 x 768 frame). Selected alone it runs in about 7 s. The
frames stay out of the repository; they are 3DMark's art.

At boot 134 (5d) the frame had trees, grass, the bridge, the fisherman
and the stream, and a white lake and a white sky.

## 1. Four coordinate sets: the lake

In the frame's run all 80 water batches were refused, 0 triangles drawn
(`ps-summary`). The new `ps-refused-draw` lines (boot 136) named the
reason: the water program samples stage 0 and, through its
`texm3x3vspec`, stage 3, and stage 3 had no texture. A log of every TEXTUREMAP under a shader (boot 137) showed 3DMark
binding the normal map to stage 0, the cube to stage 1 with coordinate
index 3, and clearing stage 3.

DDI 8 published PS 1.1 and four textures but `FVFCaps` of two coordinate
sets, the fixed-function engine's. Published as four (the wide vertex
carries four), 3DMark binds the normal map to stages 0-2 and the cube to
stage 3 (boot 138), and the water's 833 triangles are drawn, none refused.
The lake is in the frame: dark, bump-mapped, reflecting.

## 2. An empty stage reads (0, 0, 0, 1): Advanced Pixel Shader

The full benchmark after fix 1 (boot 141) still counted 175,956
texture refusals. Run test by test: Nature alone none, the two Pixel
Shader tests all of them. The logs (boots 143-144): the Advanced Pixel
Shader test sets a texture as the render target, clears all four stages,
and draws its reflection pass with programs that sample stage 0 (one
with a texkill) - sampling a stage with nothing in it, about 176,000
batches a run, every one refused.

Direct3D draws such a pixel; it does not drop it. The kernel now reads
(0, 0, 0, 1) for a sampled stage without a texture instead of sending to
the sampler, and the engine passes that mask (`unbound`) instead of
refusing; it is part of the kernel cache key. The reference interpreter
does the same. **The value is a convention, not a measurement**: it is
what DirectX-era hardware commonly returns; no reference rasteriser or
second card was compared. Host test first (`tex unbound`, `texkill
unbound`): it failed, then passed. Boot 145: texture refusals 0 in the
Pixel Shader tests (176,573 before).

## 3. Shader triangles spanning 8K are clipped, not dropped

The runtime clips to the published band, ±8191, but the HD 4600 draws a
triangle spanning 8K or more wrongly (Vol 7), so such triangles were
dropped under a shader: 1,231 in Nature's benchmark run (boot 134), and
9,598 a Pixel Shader run once fix 2 let the reflection pass draw
(boot 145). The fixed-function path clips them with the core's
`v9x_r3d_clip_triangle`, which knows the 32-byte vertex only.

`v9x_r3d_clip_wide` is that clipper for the 28-float wide vertex: the same
guard band and edges, every attribute after rhw perspective-correct at a
cut (the PS kernels interpolate colours with perspective too), a plain
blend where rhw is equal. Host-tested first against a stub. The engine
cuts such a triangle to the target and draws its fan.

## With all three (boot 146, HAL 818,688 bytes)

| Run | Texture refusals | Shader triangles dropped |
|---|---|---|
| Pixel Shader + Advanced, boot 143 (fix 1) | 175,988 | 1,201 |
| the same, boot 145 (fixes 1, 2) | 0 | 9,598 |
| the same, boot 146 (all three) | 0 | 0 |
| Full default benchmark, boot 141 (fix 1) | 175,956 | 1,258 |
| Full default benchmark, boot 146 | 0 | 0 |

- **3DMark 2001 SE default benchmark** completes: 1834 3D marks
  (boot 141: 1825). It reports "not run using default settings"; which
  setting it means was not looked at. 25 PS kernels built, none refused,
  no fault caught. Recorded, not compared.
- Pixel Shader 58.6 fps, Advanced Pixel Shader 27.5 fps (boot 146).
- **V9XD32** T12-T16 and T19 (all eight programs) PASS at R5G6B5 on
  boots 140, 145 and 146.
- **Half-Life Direct3D** timedemo mwd5, windowed 640 x 480: 394 frames,
  138.3 fps, drawn by one D3D context (V9XSNA7 `Process3=HL.EXE
  d3d-contexts=1 gl-describes=0`), no fault.
- Gates: build-host.ps1 and run-checks.ps1 pass.

The Nature frame (Image Quality, boot 146) is the boot 138 one: lake
drawn, sky white.

## The white sky is missing DXT textures (boots 147-154)

Measurement builds, none kept:

- **What the white is.** Three white batches a frame and the indexed
  ones after them carry FVF 104h (XYZRHW + one coordinate set, no
  diffuse), select D3DTA_TEXTURE in stage 0 (COLOROP SELECTARG1, arg1 2)
  and have nothing bound in any stage (boot 150). Depth-tested, opaque,
  z 0.9997-0.9999. Skipped (boot 153), the frame shows a dark sky with
  the sun glare and black holes where the far shore and the mountains
  are: they are the distant terrain and the mountain range of public
  Nature screenshots, drawn without their texture.
- **What 3DMark bound.** A ring of the last 48 stage states before the
  first white draw (boot 151): the sky strips drawn first, each with its
  own texture (handles 4Bh to 98h), then every stage set to 0 and the
  terrain drawn.
- **Not the cause:** ValidateDevice (216 questions in the run, none
  refused, boot 152); projected textures and texgen claimed (boot 149, no
  change); 16-bit textures, half the texture memory (boot 150, no
  change); a texture factor (never set).
- **The cause.** DXT1, DXT3 and DXT5 published in the DDI 8 list
  (boot 154): 3DMark creates 24 DXT textures, 18 DXT1 (128 to 1024
  square, one 256 x 128) and 6 DXT3 (256 x 64, 256 square, 1024 x 256) and binds them, and no white draw
  is left. The frame was black: the engine can neither place nor sample
  a block-compressed texture. Without DXT the renderer binds nothing for
  those maps and says nothing.

So the sky needs DXT textures on the HD 4600: placed by the engine
(FourCC, no bit count, as Q8W8V8U8) and sampled as BC1/BC2/BC3, by the
fixed-function kernels as well as the PS ones.

## Ruled out

- **The ±8191 band itself** (boot 135): a HAL with a ±4096 limit, where
  nothing can span 8K, changed nothing in the frame. In that run every
  water draw was refused before the guard test anyway.
- **Texture memory for the sky** (boots 139-140): of 55 texture creates
  in the frame's run, the 12 not handled are single-level textures the
  engine leaves to DirectDraw by design, and the cube's first try.

## Not done, not checked

- **The sky is flat white**, 255 in every sampled pixel (42,500 of
  42,500 in one block), unchanged by all three fixes: its terrain and
  mountains want DXT textures (above). Not implemented.
- **Black captures**: with four coordinate sets, 2 of 8 Image Quality
  captures were all black, both on boot 138; 0 of 4 before, and none on
  boots 139-146. The black runs flipped 62 times
  with none still drawing, and drew 1,489 batches into the presented
  buffer; a good run 6,580 flip calls, 6,517 still drawing, 389 into the
  presented buffer. Not chased.
- **The frames have not been judged by Michael** beyond the two sent to
  him from boots 134 and 138.
- **V9XD32 has no case for an unbound stage or a clipped shader
  triangle**; both are host-tested and measured through 3DMark only.
- **32 bpp desktop**: every V9XD32 run here was at R5G6B5.
- Logging changed too: `ps-refused-draw` / `-tex` / `-tex2` lines in
  V9XPS.LOG for the first 24 refused shader batches, and `cube-draw`
  capped at 32 lines (one a draw had filled the 6,000-line log inside a
  single Pixel Shader test, boot 142).
