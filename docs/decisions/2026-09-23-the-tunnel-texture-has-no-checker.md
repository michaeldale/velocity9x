# The tunnel texture has no checker, and 3DMark 99 never asks for alpha test

2026-09-23, A8U4I5 (S3 Trio3D/2X `5333:8A13`, Windows 98 SE), boots 109-117,
3DMark 99 Max at 640x480x16. Follows
`2026-09-23-perspective-texturing-on-the-s3d.md`, which left the filtering
tunnel's missing checkerboard with one hypothesis: the checker is the ARGB1555
alpha bit, made transparent by an alpha test this driver ignores.

## The hypothesis, tested twice, and dead

**Alpha test.** The render states were recorded and counted, and a
pass-on-high-alpha test was expressed as the S3D's texel-alpha blend (the
same colour for a one-bit alpha texture).

| boot | build | `AlphaTestSets` | `AlphaTestFuncSeen` |
|---|---|---|---|
| 109 | states counted, emulation in, no alpha comparison caps | 0 | `0x100` (ALWAYS only) |
| 110 | the same with `dwAlphaCmpCaps` = GREATER, GREATEREQUAL, ALWAYS | 0 | `0x100` |

(`2026-09-23-trio3d-alphatest-b109-no-caps.ini`,
`2026-09-23-trio3d-alphatest-b110-caps-published.ini`.) 3DMark 99 never
enables alpha test, whether or not the device says it has one. No colour key
was set (`D3dColorKeySets=0`) and no blend pair was skipped.

**The texture itself.** A temporary probe in the sampler summarised the top
level of each mipmapped texture of 256 texels or more when it changed. With
only Texture Filtering selected it caught the tunnel's texture
(`2026-09-23-trio3d-tunnel-texel-probe-b116.ini`):

```
TexelProbeOffset=0x00258000  TexelProbeSize=256
TexelProbeAlpha0=65536       every texel's alpha bit is 0
TexelProbeDark=0             no texel below 4/31 in all channels
TexelProbeEdges=0            no adjacent pair differing by a quarter of range
TexelProbeLumaRange=0x004F000F   r+g+b from 15 to 79: a smooth gradient
TexelProbeFirst=0x088A088A
```

The texture is the gradient and nothing else. The alpha bits are all zero,
so 3DMark wrote it as X1R5G5B5 and never meant the checker to be alpha;
there is no dark texel and no edge anywhere in the 65,536. The first
capture sampled only the top row and the second counted only near-black
texels; the edge count is the one that rules out a checker of any contrast.
An earlier attempt with the 128-texel threshold was overwritten by the
loading cards (mipmapped, 128, `0x282AA0`), which is why the probe was
restricted to 256 and up.

## What that leaves

The texture in video memory has no checker, the census shows no second
texture drawn on the tunnel, and nothing was refused. The checker is
therefore something 3DMark does not send to this device at all. The
explanation that fits is a second, multiplicative pass - 3DMark's Result
Browser lists **Multiplicative Alpha Blending** among this card's missing
features, and the S3D unit has no multiplicative blend
(`2026-09-03-colour-key-and-blend-on-the-virge.md`) - but it is an
inference from what is absent, not a capture of what 3DMark would draw.
Either way it is not a driver fault that can be fixed on this engine: the
tunnel is drawn as the card can draw it, as fill rate is drawn without the
additive blend it also lacks.

## What was kept

- The alpha-test render states are recorded in the context and counted
  (`alpha_test_sets`, `alpha_test_func_seen`, `alpha_test_ref_last`,
  `alpha_test_unexpressed`; ABI `2026092301`). A test is still not drawn,
  as in S3's driver (`98DDK s3v\D3DSTATE.C:98`).

## What was removed, and why

- **The texel-alpha emulation and the published caps.** No application on
  this machine exercised them, so shipping them would change other titles'
  pictures untested; and on this card's bad blend state an alpha-0 texel
  blends to black.
- **The texel probe.** Up to 262,144 reads on every change of a large
  mipmapped texture is a real cost to any game that switches textures per
  draw. It was an instrument; its readings are above.

## Gates

`./scripts/check-tree.ps1`, `./scripts/build-host.ps1` and
`./scripts/run-checks.ps1` passed on the final tree. The final build was
installed on boot 117 and its snapshot reads `Ok=1` with the four
alpha-test keys and no probe keys.
