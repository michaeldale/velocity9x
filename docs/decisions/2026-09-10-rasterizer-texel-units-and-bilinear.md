# The sampler's per-pixel work was mostly setup, and a nested lerp cannot pay for itself

Date: 2026-09-10. Implemented and measured in existing 86Box guests only.
No physical machine ran this code; BARRY has not answered ICMP since its
2026-09-06 hang.

Step 4 of [`software-rasterizer-scalar-fixes.md`](../plans/software-rasterizer-scalar-fixes.md):
fixes 5 and 6 of that plan - texel-unit texture coordinates and a cheaper
bilinear filter - landed together, both textured-path only.

Result on the Trio64 guest, RAM target: bilinear 1.28x, depth-tested 1.25x,
alpha-blended 1.19x, point-sampled 1.19x, on top of the same day's
[scalar fixes](2026-09-10-rasterizer-scalar-fixes.md). Untextured scenes do
not move, which is what a change confined to the sampler should do. Cumulative
against `c4988fe`, measured on one boot rather than multiplied: point sampling
1.75x, bilinear 1.53x, depth 1.48x, alpha 1.41x in RAM. All twelve guest
colour/Z hashes are unchanged against both baselines, and the host pixel table
is unchanged.

## What changed

`src\display32\d3d\d3d_raster.c`, plus one new host test.

**The sampler's constants are resolved once per triangle.** They were resolved
per pixel, and some of them per *texel*: the size, the wrap mask, the bilinear
bias, and a chain of format tests inside a function called four times over for
a bilinear pixel, each call also multiplying its own row index by the pitch.
`V9X_D3D_RASTER_SAMPLER` now carries them, built in
`v9x_d3d_raster_triangle` after validation and passed to each span; a null one
is what untextured means. The two render states the pixel loop reads - the
address mode and the blend - travel in it too, so the loop needs no second
pointer to the texture.

**The format tests are gone rather than hoisted.** All three texel formats are
a field of w bits replicated up to eight, `(f << (8 - w)) | (f >> (2w - 8))`:
that is what `expand5` and `expand6` do for five and six bits, and what the
multiply by 17 does for four, since `17 * v` is `(v << 4) | v`. So three
per-channel descriptors - shift, mask and the two replication shifts -
describe every format with one decode path and no branch. A bilinear pixel
went from four calls with four format tests to four inline decodes.

**Texture coordinates reach the sampler in texel units.** The span scales with
a shift, not a multiply, because the size is a power of two and the sampler
now carries its log. Under WRAP the fold into the first repeat is
unconditional where it used to test against `TEXCOORD_MAX` first. That is
pixel-identical for a reason worth stating: the sampler's `& mask` already
discards everything above the first repeat, and the bits removed here are a
multiple of a whole texture, which is a multiple of both 65536 and 256 - so
neither the texel index nor a bilinear fraction can see them. What it buys is
the bound: the coordinate handed to the sampler is now under one repeat rather
than under thirty-three, so the scale is a shift that cannot overflow instead
of a multiply sized against 1.1 billion.

**The four bilinear weights come from one multiply.** `w11` is `fu * fv` and
the other three follow by subtraction, since `(256 - fu) * (256 - fv)` is
`65536 - 256fu - 256fv + fu*fv`. Exact integers throughout, so the weighted
sum is the same sum.

## What the plan proposed and this declines

**The nested lerp.** Fix 6 asks for the rows lerped horizontally and then once
vertically, "nine multiplies against the twelve". The cheap form of that -
`a + ((b - a) * f >> 8)` - is one multiply per lerp and three per channel, and
it is **not** pixel-identical: each step truncates where the four-weight sum
truncates once. `t00 = 0, t10 = 1, fu = 128` already differs. The plan
anticipated this and said to keep the current order if rounding moved, so the
exact sum stays and the multiply count falls by rederiving the weights
instead: sixteen multiplies per bilinear pixel become thirteen.

The two-step form *without* intermediate truncation is exact, but it costs six
multiplies per channel against the four the weight form costs. It is slower,
not faster.

**The packed-channel arithmetic.** Fix 6's second half proposes doing the
blend on packed 16-bit texels with channels split into two words. The ranges
do not allow it here: a channel weighted by the four bilinear weights reaches
255 * 65536, which needs 24 bits, so two channels cannot share a 32-bit word.
It would need the weights narrowed, which changes pixels. Not attempted.

**Texel-unit coordinates as the plan describes them.** The plan says to fold
the size into the *interpolant* at span setup. Done literally that is not
pixel-identical: the span's interpolant carries eight fractional bits of a
texture coordinate, and the pixel path truncates to whole coordinate units
before scaling. Folding the size in earlier keeps precision the old code threw
away - a *better* coordinate, and a different bilinear fraction. So the fold
is a shift in the pixel path instead, after the truncation, which removes the
multiply without touching what is sampled.

## The test this needed

The refactor made the decode format-driven and orthogonal to the filter, and
the corpus draws only RGB565 textures while the two-colour bilinear test uses
ARGB1555. ARGB4444 under the linear filter was exercised by nothing.
`test_texture_bilinear_uniform_formats` covers all three: four identical
texels with weights summing to 65536 must return the texel itself, and the
expected channels are written out by the replication rule rather than read
back from the sampler.

What the assertion can see is five or six bits, because that is what the
target stores. It catches a channel taken from the wrong field, a wrong shift,
and a replication dropped entirely - 0xa reaching 160 rather than 170 lands a
level low, and the test was watched failing on exactly that - but not the
bottom bits of an eight-bit decode. Recorded because the first attempt to
prove the test bites perturbed an expectation by one, which the packer
swallows.

## Method

Same instrument and procedure as the
[2026-09-07 record](2026-09-07-software-rasterizer-edge-stepping.md), which
the probe README documents. Baseline is `36f08d4` - the fixes 2 to 4 commit,
so this measures 5 and 6 alone - captured with `git show` and compiled into a
separate executable, both binaries differing in the rasterizer only.

- `Win98SE-Trio64`, loopback port 9871, boot 336: 800x600x16, 4 MB Trio64,
  agent 0.5.2. RAM and VRAM targets. The preceding baseline, `c4988fe`, ran
  again on this boot for the cumulative figure, so it is a measurement and not
  a product of two ratios.
- `Win86SE`, loopback port 9869, boot 574: ViRGE/DX, 1024x768x32, agent
  0.6.0. RAM only - a 32-bpp desktop yields `VramAvailable=0`.

One guest at a time, each shut down through the agent before the next started.
Three samples per workload of at least 750 guest milliseconds after a warm-up
frame, median reported. Drift, the baseline run twice on the same boot: within
0.7 per cent on the Trio64 guest, 0.1 per cent on the ViRGE guest.

Artefacts:
[`docs\probe\software-d3d-2026-09-10-sampler`](../probe/software-d3d-2026-09-10-sampler/README.md).

## Results

Trio64 guest, boot 336, median milliseconds per scene, against `36f08d4`:

| Target | Scene | Baseline | Candidate | Speedup |
|---|---|---|---|---|
| RAM | Small | 18.537 | 18.659 | 0.99 |
| RAM | Gouraud | 17.698 | 17.674 | 1.00 |
| RAM | Point | 57.143 | 48.125 | 1.19 |
| RAM | Bilinear | 137.500 | 107.857 | 1.28 |
| RAM | Depth | 144.167 | 115.714 | 1.25 |
| RAM | Alpha | 175.000 | 146.667 | 1.19 |
| VRAM | Small | 19.231 | 19.359 | 0.99 |
| VRAM | Gouraud | 20.000 | 19.974 | 1.00 |
| VRAM | Point | 88.333 | 79.500 | 1.11 |
| VRAM | Bilinear | 255.000 | 226.250 | 1.13 |
| VRAM | Depth | 295.000 | 265.000 | 1.11 |
| VRAM | Alpha | 353.333 | 325.000 | 1.09 |

Cumulative on the same boot, `c4988fe` against this checkout: RAM point 1.75x,
bilinear 1.53x, depth 1.48x, alpha 1.41x, Gouraud 1.15x, Small 1.04x; VRAM
1.45x, 1.25x, 1.21x, 1.18x, 1.13x, 1.04x.

ViRGE guest, RAM only, against `36f08d4`: bilinear 1.27x, depth 1.25x, alpha
1.19x, point 1.19x, Gouraud and Small unmoved.

## What the numbers say

**The sampler's cost was mostly not sampling.** The scene that gains most is
bilinear, at 1.28x, and the four texel fetches it makes did not change: the
same words are read from the same addresses. What went was four calls, four
format tests, two of the four pitch multiplies and three weight multiplies.
In a build that inlines nothing, the setup around a memory access was worth
more than a quarter of the pixel.

**Point sampling gains as much as alpha blending.** Both 1.19x, which is not
obvious - the alpha scene does strictly more per pixel. It follows from where
the saving is: one call and one format test per pixel is a fixed amount, so it
is a larger fraction of the cheaper scene, and the alpha path's extra work
(an unpack, three blends) is untouched by this change.

**The VRAM gain is about half the RAM gain, again.** Bilinear 1.28x in RAM
against 1.13x in emulated video memory, point 1.19x against 1.11x. The same
pattern the scalar fixes showed, and the same caveat: this is 86Box's model of
an aperture, and the plan's step 1 physical read timing is still unrun.

**Nothing here says anything about a game.** Synthetic scenes at 320x240 on
an emulated Pentium MMX. A bilinear scene that took 137 ms now takes 108.

## Limits

- Emulator only. No physical timing, no frame rate from a named application.
- The pixel-identity claim rests on the host table under Open Watcom - whose
  frozen corpus covers RGB565 textures, both filters, both target formats,
  modulate, alpha and random coordinates across the full thirty-three repeats
  - plus the new uniform-texture test and eighteen guest colour/Z hashes.
  `build-host-msvc.ps1` still cannot run on this host: no Visual Studio, so no
  `vswhere.exe`.
- `run-checks.ps1` passed on the final source, so the HAL builds for all four
  families.
- **The installed HAL was then run, and agrees.** The benchmark links the
  rasterizer directly and never loads the driver, so it says nothing about the
  engine's own vertex conversion or caps. The DirectDraw probe through the
  installed HAL does: on the Trio64 guest with `Direct3D=2` -
  `Direct3DMode=software`, which is the configuration in which every Direct3D
  draw goes through this rasterizer, the Trio64 having no S3D engine -
  `compare-probe.ps1` reports zero differences across 1117 keys between the
  43,520-byte `soft-edge-01` HAL and the 44,032-byte HAL carrying both of this
  day's commits, `Result=COMPLETE` on both. `RampOk`, `SpriteOk`,
  `MipLadderOk` and `AlphaCurveOk` read 0 in both, as they did in both
  2026-09-07 runs: they are texel-alpha and mip rungs and this engine
  implements neither. `VtxAlphaCurveOk` reads 1.
  [Artefacts](../probe/software-d3d-2026-09-10-sampler/README.md), boots 337
  and 338.
- The plan's remaining item is fix 7, paired stores, which its own text defers
  until the physical aperture write measurement exists.
