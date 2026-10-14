# The Rage XL keys textures by comparing the raw texel, not the 24-bit expansion its register reference describes

Date: 2026-10-14

Status: Measured on A8U4I5 (ATI 3D Rage XL PCI), boots 473-475.
Evidence: `docs/probe/a8u4i5-rage-xl-gamma-colour-key-2026-10-14/`.
Applies to the Mach64 (Rage Pro) class. The Rage II path still refuses
colour keys.

## Why

Thief II on the Rage XL had 45,018 draws refused with reason 18,
`COLOR_KEY`, within minutes. The Mach64 policy refused every keyed draw.
V9XDDP's `ColorKeyOk` passed regardless. Its texture is all key, drawn
over a cleared target, so a refused draw leaves the same black pixel as a
keyed one.

## The hardware

The XL/XC register reference (5-59 to 5-61) and xf86-video-mach64's
`atiregs.h` both name the colour compare:

- `CLR_CMP_CNTL` with `CLR_CMP_SRC_TEXEL` (02000000h) and `FN_EQUAL` (5)
  keeps the destination where the texel equals `CLR_CMP_CLR`, under
  `CLR_CMP_MSK`.
- The reference says the texel is compared expanded to 24 bits.

## The measurement

**First build, boot 474.** It used the reference's expanded key: `F800F8`
under mask `F8FCF8` for a 565 key `F81F`. Keyed draws now reached the
screen, and the key colour drew: V9XDDP's new `ColorKeyHalves` read
`7C1F`, magenta, on the keyed side.

**`ATIRX /pro /ckey`, boot 474.** One 8x8 RGB565 texture, `F81F` on the
left and green on the right, drawn over blue, once per encoding:

| Scene | CLR_CMP_CNTL | CLR / MSK | Left (key) | Right |
|---|---|---|---|---|
| CkBuilder | 02000005 | 00F800F8 / 00F8FCF8 | F81F, drawn | 07E0 |
| CkFullRep | 02000005 | 00FF00FF / FFFFFFFF | F81F | 07E0 |
| CkFullTrunc | 02000005 | 00F800F8 / FFFFFFFF | F81F | 07E0 |
| CkRaw565 | 02000005 | 0000F81F / 0000FFFF | **001F, kept** | 07E0 |
| CkRep24 | 02000005 | 00FF00FF / 00FFFFFF | F81F | 07E0 |
| CkRaw565Full | 02000005 | 0000F81F / FFFFFFFF | **001F, kept** | 07E0 |
| CkTrue | 02000001 | - | 001F | 001F |
| CkNotEqualZero | 02000004 | 0 / FFFFFFFF | 001F | 001F |
| Ck2dSrc | 01000005 | 00F800F8 / 00F8FCF8 | F81F | 07E0 |

The compare takes the raw texel in the texture's own format. `FN_TRUE`
keeps everything, which shows the texel source does engage. Every
24-bit encoding misses.

## The change

- **`mach64_draw.c`:** a keyed textured draw sets `CLR_CMP_CNTL`
  (state slot 2, which every draw already writes as 0) and appends
  `CLR_CMP_CLR` and `CLR_CMP_MSK`. The key goes in raw, and the mask
  leaves alpha out:

  | Format | Mask |
  |---|---|
  | 565 | FFFF |
  | 1555 | 7FFF |
  | 4444 | 0FFF |

- **`mach64_policy.c`:** accepts a keyed draw when the bound texture
  carries a key. With no texture, or a texture without a key,
  COLORKEYENABLE has no effect, as Direct3D defines it. A keyed
  composite is still refused, being unmeasured.
- **`d3d_mach64.c`:** reads the key from the surface record
  (`HASCKEYSRCBLT`, `ddckCKSrcBltLow`), as the Matrox path does, and
  claims `D3DPTEXTURECAPS_TRANSPARENCY`.

Host tests were written first and failed before each step: the policy,
the state words, and the map.

## Result

| Boot | ColorKeyHalves | Whole V9XDDP run |
|---|---|---|
| 473 (refused) | both halves blue, `Ok` 0 | 206/224 |
| 475 | 565 and 1555: left `001F` kept, right `03E0` drawn, `Ok` 1 | 207/224 |

No other key changed. In Thief II at boot 475, no draw was refused in
the mission (494,541 draws).
