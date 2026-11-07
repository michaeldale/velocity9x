# GMA 950, 3DMark 2001 SE: Dragothic's sky draws white

Date: 2026-11-07. Seen by Michael on the netbook's panel (W9QI2BE2,
945GSE / GMA 950), build `089a5e70`, boot 284, 3DMark 2001 SE default
benchmark at 640x480, 16-bit colour and 16-bit textures. Status: open,
investigated 2026-11-07, cause not found (below).

## What is known

The counters of the run (`../probe/gen3-depth-edge-2026-11-07/watched-pre.INI`
and `watched-post.INI`, whole 3DMark runs, not Dragothic alone):
- No texture refused: `D3dTextureCreates` 68,601 = `D3dTextureDestroys`,
  every `D3dTextureRefused*` 0.
- `MipTreeDeclined` rose by 7,486 in the run, last reason 4 (no room for
  the driver's own placement). A declined chain is left to DirectDraw's
  heap, where Gen3 can sample only its top level (`d3d_i9xx.c`, MIP
  TREES). That leaves a texture without its smaller levels, not a white
  surface.
- 0 batches refused.

So the sky is not the lost-texture or refused-batch pattern those
counters can show.

## Candidates, none tested

- The sky's material uses a second texture stage, which Gen3's Direct3D
  does not offer (`texture_stages` 0), and the remaining stage draws
  white.
- A stage operation or blend the engine expresses differently.
- A refused draw falling to the CPU fallback, which cannot sample a
  surface texture in Gen3's mip layout (`v9x_r3d_fallback` returns
  UNSUPPORTED for those).

It looks like the HD 4600's open Nature defect (distant terrain and
mountains white, 2026-10-30-haswell-ddi8-ps11-record.md), but that is a
resemblance, not a finding.

## 2026-11-07: measured - nothing is bound

Dragothic alone (low and high detail) on boots 285 and 286.
- Boot 285 ran `efd2fa90`. Boot 286 ran `39893580-dirty`, which adds the
  state census to indexed draws, which Gen3 takes whole and which the
  census had missed.
- Census and frame grab armed (`V9XTRACE -omit:2147483648 -grab:60x10`).
- Evidence:
  [`../probe/gen3-dragothic-sky-2026-11-07/`](../probe/gen3-dragothic-sky-2026-11-07/)
  (`b286-post.INI`, `boot285-V9XGRAB1-low-detail.png`, `boot285-V9XGRAB6-high-detail.png`).

**The frames** (`V9XGRAB1` on boot 285, low detail; `V9XGRAB6`, high
detail) show the sky flat white around the tower, and a white wedge in
a corner of the high-detail frame.

**The draws.** `DrawsNoHandle` is 1,894 and `DrawsHandleUnresolved` 0.
So the white geometry reaches the driver with **no texture bound** at
all, not with a texture the driver lost. Census slot 8 (`0x28700900`,
194 batches) is such a state:
- untextured, depth test and write on, no blend;
- diffuse `FFFFFFFF`;
- MIPLINEAR and LINEAR filters and wrap addressing set, as if a texture
  were meant.

Slots 3 and 7 are untextured multiply passes (DESTCOLOR x ZERO) with the
same leftover filters. `Dp2Stage1States` is 0: nothing is set on a
second stage either.

This is the HD 4600's parked Nature defect
(`docs/plans/haswell-ddi8-pixel-shaders.md`, Parked): 3DMark's renderer
binds NULL because the material's map has no device texture, and no
error reaches the driver. That investigation ruled out DXT, texture
size, formats, ops, address modes, projection, a depth-only pass, a
94 MB texture budget and render-to-texture.

What is particular to the GMA 950: its DirectDraw heap is about 6 MB,
and each 3DMark run declines thousands of mip chains for room
(`MipTreeDeclined` +10,288 on these boots). A chain the HAL declines
goes to DirectDraw's own heap. If that allocation fails, the error goes
to the application without passing anything this driver counts. So a
material whose texture never fit is a live hypothesis here, which it
could not be on the HD 4600. It is not measured.

## Next

- **Test the heap hypothesis.** Count texture surface creations the HAL
  declines, with the free heap at that moment. Then see whether any
  are declined when there is too little room for DirectDraw to place
  them.
- **Compare the same run on a card with a large heap** running
  Velocity9x (an Evergreen or the HD 4600). If its Dragothic sky is
  white too, the heap is not it, and this joins the Nature defect.
- A reference frame from real DirectX 8 hardware would settle what the
  sky should be.
