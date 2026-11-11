# The GMA 950 samples ARGB8888 and XRGB8888 texels exactly, and 3DMark 2001 SE runs on them

Date: 2026-11-11. Machine: the netbook (W9QI2BE2, GMA 950, 945GSE),
boot 301. Build 25e042f1 (clean): DRV, VXD, HAL and ICD deployed by
WININIT rename, all four hashes matching the package. Step 5a of
[`../plans/gen3-ps11-pixel-shaders.md`](../plans/gen3-ps11-pixel-shaders.md).

## Question

25e042f1 made the Gen3 engine place and sample `MAPSURF_32BIT` ARGB8888
and XRGB8888 maps, and publishes both as texture formats to every
Direct3D title. Two things were unmeasured:

- whether the GMA 950 reads those codes as the two reference drivers
  name them, byte order included;
- whether titles that now receive 32-bit textures still draw.

The signed V8U8 and Q8W8V8U8 maps from the same commit are not
published until `draw_shader` exists, and were not exercised here.

## T22, a new V9XD32 test

V9XD32 had no fixed-function 32-bit texture test: T6's map is
ARGB1555, and T19 needs pixel shaders. T22 draws T6's 8x8 quadrant map,
DECAL and nearest, as A8R8G8B8 at (400, 100) and as X8R8G8B8 at
(480, 100), and judges each quadrant's centre against its texel within
8 a channel, T6's tolerance. The texels are 0xFF2040C0, 0xFFC08020,
0xFF10E070 and 0xFF90A0F0: red and blue differ in every one, so a
sampler reading the bytes in the other order would fail. V9XD32 built
`t22-25e042f1-dirty`, from C:\V9XT22.

| Mode | T22A (ARGB8888) | T22X (XRGB8888) | Run |
|---|---|---|---|
| 640x480x32 | 2040C0 C08020 10E070 90A0F0, `Ok=F` | the same, `Ok=F` | PASS, 0 failures |
| 640x480x16 (`/bpp16`) | 2217 BC04 16EE 951D, `Ok=F` | the same, `Ok=F` | PASS, 0 failures |

At 32 bpp every texel came back exact. At 565 each is its texel rounded
to 5:6:5 (2217 is 21/41/BD for 20/40/C0). The HAL drew them: the V9XTRACE
snapshot after the first run (driver and HAL `0.47.0 25e042f1`,
`BuildsAgree=1`) has `I9xxDrawsSubmitted=1604`, `I9xxDrawsRefused=0`,
`D3dTextureRefusedFormat=0`, `I9xxTextureDraws=11`. Every draw went to
the engine and none fell back.

## 3DMark 2001 SE, all 17 tests

3DMark 2001 SE was set to 32-bit textures, as the netbook's copy already
was. The Texture Format list offered 16 bit and 32 bit, with no
compressed entry, since Gen3 publishes no DXT. Run twice at 640x480,
first 16-bit then 32-bit colour, D3D software T&L, game detail Low. The
counters below are differences between V9XTRACE snapshots taken before
and after each run, in the same boot.

| Colour | Score | Engine draws | Refused | Textured draws | Texture format refusals | System-memory textures |
|---|---|---|---|---|---|---|
| 16-bit | 489 | 1,202,909 | 0 | 963,272 | 0 | 0 |
| 32-bit | 466 | 1,169,964 | 0 | 930,338 | 0 | 0 |

Both runs completed. The Dragothic frame seen mid-run was textured
throughout. The scores are recorded and not compared: these are not the
settings of any earlier netbook run.

## What this does not show

- **Whether 3DMark used 32-bit textures.** No counter records the format
  of a placed texture. The last mip tree's shape (`MipTreeLastShape`
  0x00400018: 64 bytes by 24 rows, for a 16x16 texture) is the same at
  two and four bytes a texel, because the pitch aligns to 64 bytes.
  3DMark asked for 32-bit textures, and the format was published; that
  it got them is inferred.
- **The signed formats.** V8U8 and Q8W8V8U8 are still unpublished and
  were not sampled. Their channel order and range remain read from the
  names (5f).
- Mip chains at four bytes a texel were placed (3DMark made 115,000
  trees across both runs, none refused). Only T22's single-level maps
  were checked pixel by pixel.

## Side observation

T17, recorded and not judged, reports the Direct3D 8 HAL caps call
failing on this build (`T17D3D8HalCapsHr=0x8876086A`,
D3DERR_NOTAVAILABLE). It bears on the plan's open question of whether
DDI 8 reaches Gen3 at all. It was not investigated here. 3DMark 2001 SE
itself reached the HAL (the counters above); which DDI level it came
through was not checked.

## Evidence

V9XD32 reports and snapshots are in session 7b86a0ab's scratchpad
(`nb301-d32-16.txt`, `nb301-d32-bpp16.txt`, `nb301-snap*.ini`), and the
score screenshots are there too; not in the repo.
