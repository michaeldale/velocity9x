# The Trio3D reads the texture stride from the 3D stride register

Date: 2026-09-03
Status: measured on A8U4I5 (S3 Trio3D/2X) and on the emulated ViRGE/DX; fixed

## The picture

3DMark 99 on the Trio3D, with the pair that had just fixed the green wall:
every texture drawn as saturated noise with recognisable fragments - the sky,
the mech, the billboards, even the benchmark's own on-screen text - while
Final Reality on the same card, in the same session, drew cleanly with 74,719
texel-alpha draws and clean counters. The emulated ViRGE/DX drew 3DMark's
textures correctly with the same driver.

## The measurement that separated the applications

The trace block said Final Reality's mip chains are three levels deep with a
level-1 delta of 8,192 bytes: its textures are 64 texels across, every one of
them. 3DMark's last sampled texture was 128 across, and the refused one the
evening before was 256. The probe had only ever drawn 64-texel textures.

A new probe rung fills 64-, 128- and 256-texel ARGB1555 textures green on the
left half of every row and blue on the right, and draws each half separately
with COPY blending:

```
                      64            128           256
Trio3D, before   992 / 31 ok    155 / 868      279 / 744      (left / right raw)
Trio3D, after    992 / 31 ok    992 / 31 ok    992 / 31 ok
ViRGE/DX 86Box   992 / 31 ok    992 / 31 ok    992 / 31 ok    (before and after)
```

992 is green in ZRGB1555, 31 is blue. The "before" 128 and 256 values are
mixtures with the halves largely swapped: the sampler was walking the texture
with the wrong row length.

## What it was

`V9X_VIRGE_3D_DEST_SRC_STRIDE` carries the destination stride in its high half
and a source stride in its low half. The engine wrote the screen pitch into
both. The ViRGE/DX derives texel addresses from the command word's size field
alone and never reads the source stride for textures; 86Box's model
(`buildeference-vid_s3_virge.c:1789-1790`) routes that half to the 2D source
stride variable and its texel fetches never touch it. The Trio3D's texture unit
does read it as the texture's row pitch. A 64-texel texture is 128 bytes a row
and happened to survive - why, exactly, is not known; possibly the unit
applies the stride only above some size - but 128 and 256 did not.

The low half is now the texture's own pitch, `2 << size_log`, whenever a
texture is bound, and the screen pitch otherwise. Every other probe key on
both machines is unchanged by the change, the emulator's `D3DZWriteMaskOk`
alternation aside.

## What this says about the earlier Trio3D record

`docs/issues/2026-09-03-trio3d-alpha-and-mip-differ-from-virge-dx.md` recorded
mip level selection as differing between the card and the emulator. Those
observations were made with the wrong stride in this register on every
mipmapped draw; they should be re-measured before anything is built on them.
Vertex alpha on the card (`D3DVertexAlphaBlendOk=0`, raw 527, unchanged today)
does not involve a texture and stands.

Gates: check-tree, vga survey safety gate, host tests and family packages all
green (run-checks).

## Open

- 3DMark 99 on the Trio3D with this build, by eye. The flat green panels in the
  first game scene remained after the bounds fix with zero refusals and zero
  probe-green draws, so they are something else again - the same counters on
  the next run are the first thing to read.
- Why a 64-texel texture survived the wrong stride.
