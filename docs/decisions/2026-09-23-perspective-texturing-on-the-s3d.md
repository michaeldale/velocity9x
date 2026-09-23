# Perspective texturing on the S3D, and what the tunnel's missing checker is not

2026-09-23, A8U4I5 (S3 Trio3D/2X `5333:8A13`, Windows 98 SE), boots 107 and
108, 3DMark 99 Max at 640x480x16 with only Fill Rate, Texture Rendering and
Texture Filtering selected. Follows
`2026-09-23-the-dx5-draw-paths-never-clipped.md`. The card was in its bad
blend state throughout (`TexMatrixOk=90` of 117 on boot 107).

## What was wrong

The device published `D3DPTEXTURECAPS_PERSPECTIVE` and drew every textured
triangle with the affine command types, LitTex and UnlitTex. A receding
polygon then carries its texture linearly in screen space. On the filtering
tunnel that is walls warped into chevrons, and the one mip level chosen per
triangle from the average density was a coarse one
(`../images/3dmark99-trio3d-2026-09-23-tunnel-affine-vs-perspective.png`,
left).

## Change

- **Perspective command types.** A textured triangle whose three rhw values
  differ is drawn with LitTexPersp / UnlitTexPersp (`+0x20000000`,
  `98DDK s3v\VIRGE1.H:179-182`) and the W registers at `B50Ch`-`B514h`.
  The encoding is S3's (`GENTRI.C:611-791`): W rescaled so the largest vertex
  is 256, 13.19; U and V in texels relative to a whole-texel base, times W,
  20.12 shifted right four (`DXGX`, `D3DCTXT.C:167`, the DX/GX setting); the
  base in TBU/TBV as texels x 2^(16 - size_log), with S3's `uBaseHigh` bit 31
  in TBU. Every scale was checked against 86Box's
  `tex_sample_persp_*_375`, which both parts this backend binds are modelled
  with: `(U * 2^46 / W) >> (8 + size_log)` gives back the `tu * 2^27` the
  affine path writes.
- **Per-vertex mip level.** Under the perspective types the level is
  computed at each vertex from the Jacobian of the projective mapping and
  interpolated through DS / DDDX / DDDY, as `_SETUP_D_PERSPECTIVE` does
  (`GENTRI.C:210-296`). The rule is log2 of the longer screen-axis
  footprint, not S3's larger singular value, because it needs no square
  root; they differ by at most half a level. Clamped per vertex to the
  levels the chain has.
- **Perspective-correct clipping.** `v9x_d3d_lerp_vertex` blended `tu`/`tv`
  linearly, which is right only when the two ends share an rhw. It now
  blends `tu * rhw` and divides by the blended rhw. Without this the
  perspective build still squeezed each clipped wall's texture into its
  visible part (boot 107's frames); it is also a correction for every engine
  that takes the execute-buffer path.
- **Unchanged by construction:** a triangle with three equal rhw values -
  every probe rung, every screen-aligned quad - is emitted exactly as
  before, and the two-pass trilinear (ViRGE/DX only) stays affine.

## Measured

Boot 108, fresh, 3DMark first (`2026-09-23-trio3d-3dmark99-perspective-V9XSNA5.ini`):

- The tunnel draws with `0xB4E0x047`, the old `0x94E0x047` plus the
  perspective bit: 4,015 draws per mode against 502 affine. Fill rate's
  planes are `0xB4E06047`, 33,376 draws.
- `TrianglesDeclined=0`, `BatchesEngineRefused=0`, no engine timeout or
  reset, no texture refusal, `D3dMipChainGaps=0`.
- The tunnel walls converge on the vanishing point
  (`../images/3dmark99-trio3d-2026-09-23-tunnel-affine-vs-perspective.png`,
  right). The texture-rendering tiles are sharper
  (`../images/3dmark99-trio3d-2026-09-23-texrender-perspective.png`) and so
  is the fill-rate lettering (`../images/3dmark99-trio3d-2026-09-23-fillrate-perspective.png`).
- 3DMark's own figures: fill rate 9.2 Mtexels/s single, 34.2 multi (73.6 and
  73.5 before this and the clip change - those timed frames that drew
  nothing); texture rendering 20.5 / 14.7 / 8.3 / 4.3 / 2.3 fps, 2-32 MB
  (21.0 / 14.7 / 8.5 / 4.6 / 2.3 on boot 103); filtering point 215.0 %,
  bilinear 100 %, trilinear 103.6 %.

The probe on boot 107 (`2026-09-23-trio3d-probe-b107-perspective-build-V9XDD2.ini`)
reads `Result=COMPLETE`, `TexMatrixOk=90`. Against boot 104 on the previous
build, every key that differs is an allocation address or a blended rung,
and the blended rungs differ as much between boots 103 and 104, both on the
old build. No unblended rung moved. The probe draws every triangle with rhw
1.0 on all three vertices, so it reaches none of the new path: this is a
regression check of the affine encoding, not a test of the perspective one.

## The checker is not a mip or perspective problem

With perspective in, the tunnel still has no checkerboard. The point-sampled
frame near the screen edge shows texels magnified to roughly 30-40 pixels,
which is level 0 of a 256-texel texture wrapped a few times along the
tunnel - correct magnification, not a coarse level. The blocks are the size
of the reference's checker squares, and their colour is the gradient only.

The hypothesis this leaves: the checker is the ARGB1555 alpha bit, alternate
texels alpha 0, and 3DMark makes them transparent with alpha test against
the black background. This driver ignores `ALPHATESTENABLE`, `ALPHAREF` and
`ALPHAFUNC` entirely, and so did S3's (`D3DSTATE.C:98, 132`, commented out),
and the tunnel's command word carries no alpha bits. **Not verified.** The
driver's last-texture sample cannot test it: 3DMark's loading cards are
textured (ARGB4444, 128, `0x282AA0`) and are always the last draw, so the
tunnel's texels were never the ones sampled. A texel sample per census row,
or a count of the alpha-test states an application sets, would.

## Gates

`./scripts/check-tree.ps1`, `./scripts/build-host.ps1` and
`./scripts/run-checks.ps1` passed. Untested on the emulated ViRGE/DX, on the
software rasterizer (which takes the perspective-correct clip only) and on
Gen3 (execute-buffer clipping only; its DX5 paths do not clip in the core).
