# GMA 950, 3DMark 2001 SE: Dragothic's sky draws white

Date: 2026-11-07. Seen by Michael on the netbook's panel (W9QI2BE2,
945GSE / GMA 950), build `089a5e70`, boot 284, 3DMark 2001 SE default
benchmark at 640x480, 16-bit colour and 16-bit textures. Status: open,
not investigated.

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

## Next

Run Dragothic alone with the state census armed (`V9XTRACE
-omit:2147483648`, V9X_DD_MEASURE_CENSUS). That gives the draw states
the engine was handed, with their textures and stage keys. Add a frame
grab (`V9XTRACE -grab`) so the white sky can be tied to one state. The
same run on the 0.39.0 release would say whether this is new.
