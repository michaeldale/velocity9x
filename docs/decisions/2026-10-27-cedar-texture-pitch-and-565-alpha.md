# HD 5450 textures: rows are whole 64-byte units, and 565 samples alpha as 0

Date: 2026-10-27

Status: **measured, one card.** M-BXMFA8's HD 5450 (`1002:68F9`, Cedar),
boots 19 to 22. Follows
[the HAL's first light](2026-10-27-cedar-d3d-hal-first-light.md). Tools:
V9XDDP (the current build's) on the HAL, compared key by key with the GMA
950 netbook's 0.28.0 report
([`nb234-V9XDD.INI`](../probe/0.28.0-release-checks-a8u4i5-netbook-2026-10-23/nb234-V9XDD.INI))
by `scripts\compare-probe.ps1`; V9XEG3D `/d3d1555` for the measurement.
Captures: `V9XDD-b19-1`, `-b21-1` and `-b22-1` in
[`../probe/m-bxmfa8-cedar-d3d-hal-2026-10-27/`](../probe/m-bxmfa8-cedar-d3d-hal-2026-10-27/);
`V9XEG3D-h*`, `-i*`, `-j*` and `-k*` in
[`../probe/m-bxmfa8-cedar-d3d-streams-2026-10-27/`](../probe/m-bxmfa8-cedar-d3d-streams-2026-10-27/).

## Texel phase, and the pitch it found

V9XDDP's TexPhase draws a 16x16 ARGB1555 texture 1:1. Texel (x, y) holds
2x in red and 2y in blue, and the test reads which texel each pixel took.
On boot 19 the columns read `0123456789ABCDEF` at offset 0 and
`.0123456789ABCDE` at +0.5, as the GMA 950's do, so the PA's
whole-coordinate pixel centres are Direct3D's. The rows read
`0022446688AACCEE`: every odd texel row came back as the even row below
it, even at +0.25, a quarter texel inside the row.

The probe could not tell a lost blue bit from wrong rows, so V9XEG3D
`/d3d1555` sampled the probe's pattern as a 1555 texture and checked every
field of every texel:

| Texels | Row pitch | Run | Result |
|---|---|---|---|
| 1555, 64 wide | 128 bytes | h1, i5 | exact: B, G, R and A all 0 mismatches |
| 1555, 16 wide | 32 bytes | i1, j6 | 256 of 512 wrong, from row 1 |
| 1555, 16 wide | 48 bytes | j4 | 384 wrong |
| 1555, 16 wide | 64 bytes | i2 | exact |
| 1555, 16 wide | 80 bytes | j5 | 384 wrong |
| 1555, 16 wide | 128 bytes | i3 | exact |
| 1555, 32 wide | 64 bytes | i4 | exact |
| 1555, 16 wide | 192 bytes | k2 | exact |
| 8888, 8 wide | 32 bytes | j1 | 256 wrong |
| 8888, 16 wide | 64 bytes | j2 | exact |
| 8888, 16 wide | 96 bytes | j3 | 256 wrong |
| 8888, 16 wide | 128 bytes | k3 | exact |
| 8888, 16 wide | 192 bytes | k1 | exact |

1555's fields are as assumed: blue is the low field, and the format reads
D3D's ARGB1555 with no swizzle. The rule is in bytes, not texels, and it
is "a multiple of 64", not "a power of two": 192 was exact in both
formats. Linear-general texture rows are whole 64-byte units.
`create_surface` rounded to 8 texels, which gave TexPhase's texture
32-byte rows. `v9x_eg_3d_texture_pitch` now rounds to 64 bytes, and the
builder refuses anything else. The host test holds both. Boot 21's TexPhase
rows read `0123456789ABCDEF`, every phase matching the GMA 950.

## A texture without alpha must be opaque

V9XDDP's untagged texture (no pixel format, so the 565 desktop's) is
filled with green, drawn MODULATE under SRCALPHA / INVSRCALPHA with opaque
white vertices, and must come out as the texel. On Evergreen it came out
as nothing (`D3DUntaggedTextureRaw=0`). FMT_5_6_5 has no fourth component,
and the resource's DST_SEL_W asked for it, which reads 0. The product's
alpha was 0 and the blend drew nothing. A 565 texture now selects the
constant 1 for W. The host test was written first and failed. Boot 22
matched the GMA 950.

## Mip chains: the layout the sampler uses

Before the HAL can offer mip chains, it must place them as the sampler
addresses them. V9XEG3D `/d3dmip:K` laid a 32x32 8888 chain over a pattern
by the layout Linux's `evergreen_cs.c` checks for `ARRAY_LINEAR_ALIGNED`:
every level's pitch `MAX(64, 256 / bpe)` texels, rows unpadded, the levels
one after another from `MIP_ADDRESS` (level 1), level 0 at `BASE_ADDRESS`.
It then held the sampler at level K with its LOD clamp and point mip
filtering:

| Run | LOD clamp | Result |
|---|---|---|
| m1 to m6 | K x 64 (4.6 fixed, R600's) | level 0 right; K = 1 read level 0, K = 2 to 5 read level 1 |
| n1 to n6 | K x 256 (4.8 fixed) | every level exact: offsets 0, 2000h, 3000h, 3800h, 3C00h, 3E00h |

Evergreen's `MIN_LOD`/`MAX_LOD` have 8 fractional bits. The m runs read
a quarter of the asked LOD, rounded. Those runs already put model level 1
exactly where the model says, and the n runs confirm every level. Only
8888 has been measured. The 16-bit pitch (128 texels, 256 bytes) is the
same formula's answer, not a measurement.

## Mip chains in the HAL (boot 24)

`create_surface` now places a mip chain in one block by
`v9x_eg_3d_mip_layout`. `d3d_eg.c` recognises a bound chain when every
level sits where the layout puts it. The builder then sends it linear
aligned with `MIP_ADDRESS`, `LAST_LEVEL`, the mip filter and `MAX_LOD` in
4.8. Direct3D's four mip filters map to a within-level filter and a mip
filter, as the Rage 128 engine reads them. The caps now offer them.

V9XDDP on boot 24, against the GMA 950: every mip and trilinear check
the GMA 950 passes now passes here too (`ChainA/D/EOk`, `MipLadderOk`,
`MipTriOk`, `D3DMipmapLevelSelectOk`, `D3DTrilinearBlendOk`). `ChainBOk`,
`ChainCOk` and `ChainRowsOk` pass here and fail there. V9XD32 still passes
at 16 and 32 bpp. So the 16-bit chains V9XDDP draws confirm the 16-bit
pitch the formula gave.

## What V9XDDP still shows

Against the GMA 950, boot 24, the `_Ok` keys that differ:

| Feature | Keys | Why |
|---|---|---|
| Fog | `D3DFogMulOk`, `D3DFogTexOk`, `D3DTableFogOk`, `D3DDepthFogOk` | not drawn |
| Colour key | `ColorKeyOk` 1 here, 0 on the GMA 950 | |
| 8888 halves | `Tex8A8888HalvesOk` 1 here, 0 there | |
| Chains | `ChainBOk`, `ChainCOk`, `ChainRowsOk` 1 here, 0 there | |

The rest of the 106 differing keys are those tests' raw readings,
addresses and placement deltas.

Correction (boot 26): this comparison read `V9XDD.INI` alone. V9XDDP
continues its report in `V9XDD2.INI` once the first file nears 24 KB, and
that half was not fetched on boots 19 to 24. Its comparison is in
[the specular and fog record](2026-10-27-cedar-specular-and-fog.md).
