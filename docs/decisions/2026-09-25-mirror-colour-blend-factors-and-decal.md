# Mirror addressing, the colour blend factors and DECAL: four more quality tests pass

2026-09-25, MICHAEL-NETBOOK (945GSE / GMA 950), Win98 SE, boot 7. Driver
0.8.0 `intel-gma` with only `V9XHAL.DLL` replaced (101,376 bytes, CRC
`246742EE`), by WININIT.INI rename. The three "quick wins" from the 3D
WinBench 98 backlog
(`2026-09-24-3d-winbench-98-quality-on-the-netbook.md`), each a value the
hardware already has in a field the stream already carries.

## What changed

**Mirror texture addressing.** The sampler chose WRAP or CLAMP_EDGE from a
boolean. `struct v9x_i9xx_texture.wrap` and the decode limits'
`texture_wrap` now take `V9X_I9XX_ADDRESS_CLAMP` (0), `_WRAP` (1) or
`_MIRROR` (2) - 0 and 1 keep their old meaning - translated to
TEXCOORDMODE by two header macros the builder and the decoder share, so
they cannot disagree and no symbol is exported. An undefined value is
refused by both. `TEXCOORDMODE_MIRROR` is 1: defined in Mesa gallium
`i915_reg.h:809` and xf86-video-intel `sna\gen3_render.h:822`, used by
Mesa gallium `i915_state.c:69-70`, Mesa 21.3 classic
`i915_texstate.c:126-127` and xf86 `gen3_render.c:362`. The engine maps
`D3DTADDRESS_MIRROR` and publishes `D3DPTADDRESSCAPS_MIRROR`.

**The blend factors.** The engine accepted ZERO, ONE, SRCALPHA and
INVSRCALPHA on either side but published SRCALPHA|ONE as source and
INVSRCALPHA|ZERO as destination - so ONE/ONE was buildable and never
offered, and the benchmark reported Add Pixel Blending unsupported. The
four colour factors join them: `BLENDFACT_SRC_COLR` 3, `INV_SRC_COLR` 4,
`DST_COLR` 9, `INV_DST_COLR` 10, defined in gallium `i915_reg.h:930-937`
and `sna\gen3_render.h:240-247`, used by gallium
`i915_state_inlines.h:122-128`. Both caps now publish all eight. The
destination-ALPHA factors (7, 8) stay out: a 565 target has no alpha.

A correction found on the way: `test_runtime_blend_pairs` said codes 3 and
4 were the destination-alpha factors and asserted them unknown. Both trees
say 3 and 4 are the source-colour factors and destination alpha is 7 and
8. The test now says so.

**DECAL.** `D3DTBLEND_DECAL` is the texel alone, colour and alpha, which
is the sampling program scene 1 has run on this part (`texld` straight to
oC). `V9X_I9XX_TEXPROG_DECAL` (3) declares it for a runtime stream; the
decoder's program check is by length and the sampling program's (10) is
its own. `D3DPTBLENDCAPS_DECAL` published. DECALALPHA is not: it needs a
lerp program this engine does not build.

## Measured

Host: sampler mirror words (`0x1260` = mode 1 on three axes), decoder
equality for mirror both ways and an undefined mode refused; all 64 pairs
of the eight blend codes built and landed in S6, 7/8/11 refused; DECAL
dword-for-dword the sampling program and decoded only when declared.
Mutation check on mirror: the macro mapping MIRROR to WRAP gives three
host-test failures (sampler word and runtime decoder). The blend and DECAL tests were written before their
constants existed, so their first failing run was a compile failure, not
an assertion. `run-checks.ps1` green; scene CRCs unchanged (`01A4DE25`,
`1229FE1F`).

Netbook: the caps tab reads Add Pixel Blending, Decal Texture Blending,
Mirror Texture Addressing and Modulate Pixel Blending **ON**, DecalAlpha
OFF (`2026-09-25-netbook-3dwb98-hal-caps-quick-{1,2}.png`). A suite of
eight, overrides at DEFAULT (`2026-09-25-netbook-3dwb98-quick-wins.txt`),
"Direct3D HAL was used" on every row:

| test | before | now |
|---|---|---|
| 13 Decal Texture Blending | NotCapable | **Capable** - five decal squares at full brightness (`...-decal-pass.png`) |
| 20 Mirror Texture Addressing | NotCapable | **Capable** - seamless reflected marble (`...-mirror-pass.png`) |
| 31 Add Pixel Blending | NotCapable | **Capable** - pink logo, clouds through it (`...-add-blend-pass-zoom.png`, ours right) |
| 32 Modulate Pixel Blending | NotCapable | **Capable** - sky through the whole logo (`...-modulate-blend-pass.png`) |
| 12 Modulate, 15 ModulateAlpha, 29 Alpha Transparency, 30 Source Alpha | Capable | Capable |

Verdicts from the benchmark's captured last frame; the panel was not
watched.

## Not measured

- The other colour-factor pairings - only the pairs these two tests use.
- Mirror with linear filtering or on a non-square repeat.
- Any application other than the benchmark.
