# ATI Rage Mobility-M Phase 4 texture environment evidence

Physical target: Gateway Solo 2150, ATI Rage Mobility-M `1002:4C4D`
revision `64`, 4 MiB SGRAM, 1024x768x16 desktop, boot 11.

Phase 4 item 10 tests the three `SCALE_3D_CNTL` `TEX_LIGHT_FCN` values:
replace (0), modulate (1) and alpha decal (2). `ATI4TE.EXE` drives VxD DIOC
27, which reuses the proven 8x8 texture scene with a uniform texel and
accepts only `TEX_MAP_AEN` and `TEX_LIGHT_FCN` changes around two proven
words: unblended `0x00010081` and SRCALPHA/INVSRCALPHA `0x002C0881`. Any
other `SCALE_3D_CNTL` or `TEX_SIZE_PITCH` value fails the DIOC before MMIO is
mapped.

Four textures were used: RGB565 `0x13A3`, ARGB1555 `0xA105` (alpha set),
ARGB1555 `0x6984` (alpha clear) and ARGB4444 `0x5025`. The vertex ARGB was
`0x4D7E4925` at all three vertices. Each environment was drawn unblended,
to show the colour the texture stage produces, and blended over `0xA55A`,
to expose its alpha. That makes 24 scenes. Each scene re-uploaded its
texture into the same guarded page and pulsed `TEX_CACHE_FLUSH`.

## Build a: safe REVIEW, and what it measured

`ati-phase4-texenv-20260928-a` assumed OpenGL semantics. All 24 scenes were
safe: status `0x0001FFFF` apart from the prediction bit, uniform probes, zero
exterior, texture/guard and restoration mismatches, the expected changed
region, and no timeout or reset. None of its 96 candidate models predicted
every pixel. Six scenes disagreed with model 0, and the pixels that do not
depend on rounding identify the semantics:

| Environment | Colour | Alpha out | Evidence |
|---|---|---|---|
| REPLACE | Ct | At with `TEX_MAP_AEN`; Af for RGB565 | scenes 0-1, 6-7, 12-13, 18-19 |
| MODULATE | Ct x Cf | **At**, not At x Af; Af for RGB565 | scene 9: At=255, Af=77, and the blend was fully opaque |
| ALPHA_DECAL | lerp(Cf, Ct, W) | Af | scenes 11 and 17: the ARGB1555 alpha did not reach the blend |
| ALPHA_DECAL, RGB565 | lerp(Cf, Ct, **Af**), not Ct | Af | scene 4 |

Hypotheses killed: GL-style MODULATE alpha At x Af (scene 9 missed by 27
units), decal passing texel alpha (18 units), RGB565 decal equal to replace
(9 units), and an opaque alpha for RGB565 textures (24 units).

## Build b: PASS

`ati-phase4-texenv-20260928-b` changed only the publisher's assertions. The
VxD, scenes, textures and colours are unchanged, and its pixel dump is
byte-identical to build a's. It predicts with the measured semantics and
one arithmetic rule: bit-replicated texels, and every product and lerp
divided by 255 and truncated. It passes a scene within one 565 unit per
channel, because the exact rounding is not settled: three item 9 blend
rules still fit, and 21 of 24 scenes are exact. It also requires each
rejected semantic to miss some scene by more than one unit, so the
tolerance cannot accept a wrong environment.

Two runs were byte-identical PASS: 24 of 24 within tolerance, 21 exact,
and rejected semantics missing by at most 27, 18, 9 and 24 units. All
safety counters were zero.

| File | CRC32 | SHA-256 |
|---|---|---|
| `ATI4TE.TXT`, `ATI4TE-PASS2.TXT` | `3D214DAA` | `87FAEA1B8B872E79ADBDBBD6BAE4B2077E0288C47F30363840C590BE1D4FF8F7` |
| `ATI4TE-REVIEW-A.TXT` | `ECEB1FA7` | `2780CD36B2B32A9F54FD0E6C7CAF1D1F08EA3AD303F535A996E9EEE3D25EDBDF` |
| `ATI4TE.BIN`, `ATI4TE-PASS2.BIN`, `ATI4TE-REVIEW-A.BIN` | `6583D565` | `503125937DAC23FB11B6562D711FB229163F576170C9D4725A5F18743732EC9B` |

Each BIN holds the 64x28 RGB565 target for every scene in table order,
3,584 bytes apiece, top row first.

## Consequences for the engine

- Direct3D `D3DTBLEND_MODULATE` (C = CtCf, A = At) and `D3DTBLEND_DECAL` /
  `COPY` (replace) are native. `D3DTBLEND_MODULATEALPHA` (A = AtAf) has no
  native mode and must be refused.
- `D3DTBLEND_DECALALPHA` is native for ARGB1555/ARGB4444. On an RGB565
  texture the hardware weights by vertex alpha, so the engine must emit
  REPLACE there.
- OpenGL `GL_MODULATE` on an RGBA texture produces At, not AfAt. It is exact
  only when that alpha cannot reach the framebuffer, that is when blending
  and alpha test are off.
- Repeated texture mutation: 24 consecutive uploads to one address, each
  followed by `TEX_CACHE_FLUSH`, all sampled the new texel. Texel values
  changed between successive textures, never within one. This is partial
  evidence for the cache-visibility gate. It is not an alternating-value
  test at unchanged state.
