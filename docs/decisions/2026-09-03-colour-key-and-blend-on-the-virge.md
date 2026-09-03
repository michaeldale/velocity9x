# Colour key and blend on the ViRGE: what 3DMark 99 asked for

Date: 2026-09-03
Status: three changes, each measured on the emulated ViRGE with a new probe
rung; 3DMark 99's own picture on this build is pending

## The picture

3DMark 99's first game scene on the 86Box ViRGE/DX, with the driver pair that
had just fixed Final Reality: large flat green polygons over the walls with
saw-toothed edges where they met the wall texture, and black rectangles behind
the ship and around the bottom-left HUD element. The trace block after the run
said the engine had run clean (no FIFO timeouts, no resets, no rejected
contexts or depth buffers) and said nothing else useful, because it had no
counter for any of the three things the picture turned out to be about.

## Counters first

`V9X_D3D_DIAGNOSTICS` gained, append-only:

| field | says |
|---|---|
| `texture_refused_format/shape/last` | how many draws had their texture refused by the sampler's rules, and the last refused format or shape |
| `blend_skipped`, `blend_last_pair` | triangles not drawn because the blend pair has no S3D expression, and the last such pair |
| `color_key_sets`, `color_key_raw[6]` | HAL SetColorKey calls and the last call's block, verbatim |
| `color_key_draws`, `color_key_rewrites` | keyed draws, and texel rewrite passes |
| `lcl_tail_raw[16]`, `lcl_tail_captures` | the bound texture's surface record from dwFlags onward, at the first keyed draw |

V9XTRACE prints all of them. Two of these instruments answered a question
before any fix could be written; see below.

## Blend

The S3D unit has one blend: source × A + destination × (1 − A), with A from
the vertex or from the texel (86Box `CMD_SET_ABC_SRC` / `CMD_SET_ABC_ENABLE`,
`build\reference-vid_s3_virge.c:461-462` and the 16 bpp arm near 4386). That is
SRCALPHA over INVSRCALPHA. ONE over ZERO is no blend; ZERO over ONE draws
nothing. The multiplicative pairs lightmaps use (DESTCOLOR/ZERO, ZERO/SRCCOLOR)
and the additive ONE/ONE have no expression at all, and the engine used to draw
them opaque. Coplanar with the base pass, an opaque lightmap pass fights it for
depth, which is the saw-tooth.

`v9x_d3d_virge_alpha_bits` now classifies the pair. A pair the unit cannot
express is not drawn and is counted. The scene is then unlit where it would
have been garbage. Where A comes from for SRCALPHA/INVSRCALPHA also changed:
the texel when the texture blend asks for texture alpha (DECALALPHA,
MODULATEALPHA), the vertex otherwise - not "the texel whenever the format has
an alpha channel", because both formats this engine samples have one and what
DirectDraw's own upload leaves in that bit for a texture that never asked for
alpha is not known here.

Probe rung: target filled green, white triangle, ALPHABLENDENABLE with
DESTCOLOR/ZERO. Green is the only acceptable answer.

```
BlendModulateRaw=992  BlendModulateOk=1      D3dBlendSkipped=1  D3dBlendLastPair=0x00090001
```

Published blend caps gained ONE (source) and ZERO (destination), the two
identities the classifier accepts.

Honouring a multiplicative blend needs a rasterizer that reads the
destination, which the software engine has and the S3D does not. A
per-triangle software fallback for those pairs is the next step and is not
in this change.

## Colour key

The S3D texture unit has no chroma key. It has texture alpha - one bit in
ARGB1555, four in ARGB4444 - and the blend above keeps the destination where
that alpha is zero. So a source key is honoured by the texels: when
COLORKEYENABLE is set and the bound texture carries a source blit key, every
texel equal to the key (alpha excluded) has its alpha cleared, every other
texel has it set, and the draw uses texture alpha. The rewrite runs once per
upload, not per draw: a per-surface table remembers the key and a dirty flag
set by Unlock, by a Blt into the surface, by TextureSwap, and when the key is
first seen or changes. A HEL blit the HAL never sees does not set the flag;
that is the recorded gap.

Two things had to be measured before this worked.

**Where the key is.** The HAL gained a SetColorKey surface callback, published
in the callback flags (the probe's `CbDrvSurfFlags` went from 0x3BB to 0x3FB,
so DirectDraw saw it). DirectDraw never called it: `D3dColorKeySets` stayed at
0 across a probe that set a key on a texture. The `lcl_tail_raw` instrument
then captured the texture's surface record at the keyed draw:

```
D3dLclTail00=0x00082800   dwFlags
D3dLclTail01=0x10005000   ddsCaps
D3dLclTail02..05 = 0, 0, 5, 0
D3dLclTail06=0x00000000   D3dLclTail07=0x00000000
D3dLclTail08=0x00007C1F   D3dLclTail09=0x00007C1F   <- the key the probe set
```

The key is at dwFlags+32/+36. The two zero DWORDs before it are the
destination blit key the probe did not set; the four before that agree with
the public DDRAWI order palette, clipper, mode, back-buffer count. dwFlags bit
0x00080000 was set on this surface and on none the probe had drawn before, and
is taken as "has a source blit key". `V9X_DD_SURFACE_LCL` now mirrors those
eight DWORDs and the engine reads `ddckCKSrcBlt` from the record.

**Why the first attempt still drew nothing keyed.** With the record read
correctly, `D3dColorKeyDraws` stayed 0. The table's free-slot search called
the finder with a null surface, and the finder refuses a null surface by
design. A dedicated loop replaced it.

Probe rung: a 64×64 ARGB1555 texture filled with opaque magenta, a source key
of that magenta, COLORKEYENABLE, drawn over a black target:

```
before   ColorKeyRaw=31775  ColorKeyOk=0      (0x7c1f: the key colour, drawn)
after    ColorKeyRaw=0      ColorKeyOk=1      D3dColorKeyDraws=1  D3dColorKeyRewrites=1
```

Published texture caps gained TRANSPARENCY.

## Unchanged

Every other probe key on the ViRGE is the same across the four driver pairs of
this work, apart from `D3DZWriteMaskOk`, which alternates between runs on this
emulator independently of the driver
(`docs/issues/2026-09-03-86box-virge-ignores-depth-write-disable.md`). One run
in the middle read two of the twelve shape rungs as undrawn; the next two runs
read twelve of twelve. Recorded, not explained.

Gates: check-tree, vga survey safety gate, host tests and family packages all
green (run-checks) for each pair.

## Open

- 3DMark 99 on this build, by eye. `D3dTextureRefusedFormat/Shape` will say
  whether the solid green was a refused texture; both were zero for the probe.
- Multiplicative and additive blends are skipped, not drawn. The software
  engine is where they can be honoured.
- The software engine does not key. Its rung reads `ColorKeyOk=0`, honestly.
- None of this has run on the Trio3D.
