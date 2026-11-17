# Evergreen and the R600 class: DX5 MODULATE keeps the texel's alpha; a V9XD32 check that shows it

Date: 2026-11-17. Machine: M-NZHWBR (Daniel's Radeon HD 2400 Pro, RV610
`1002:94C3`, R600 class), over the relay; boot 45 before, boot 46 after.
Change: 148af927 (both families), and the V9XD32 check below.
Evidence: [`../probe/r6-modulate-alpha-m-nzhwbr-2026-11-17/`](../probe/r6-modulate-alpha-m-nzhwbr-2026-11-17/).

## The fault

The same as the Oland's
([2026-11-17-oland-dx5-modulate-keeps-texel-alpha.md](2026-11-17-oland-dx5-modulate-keeps-texel-alpha.md))
and the GT218's
([2026-11-17-gt218-dx5-modulate-keeps-texel-alpha.md](2026-11-17-gt218-dx5-modulate-keeps-texel-alpha.md)).
`d3d_eg_map.c` and `d3d_r6_map.c` gave MODULATE and MODULATEALPHA one
modulate. That multiplies in the vertex's alpha, where DX5 MODULATE on a
texture with alpha takes the texel's alone.

## The change

- Both families keep their slot shaders unchanged.
- A fourth op, MODULATE_RGB, exists only in the lit PS each draw
  builds: B, G and R multiplied, alpha the texel's (a MAX of R1.w with
  itself as the move).
- The maps pick it for MODULATE on a texture with alpha when the core
  has not marked the stage's alpha as the diffuse's and an alpha test or
  a blend reads it:
  - Evergreen: `has_alpha`;
  - the R600 class: ARGB1555, ARGB4444, and an ARGB8888 not marked
    opaque.
- Trinity's Cayman relocation copies ALU words unchanged; MAX already
  appears in the colour-key test.

Host tests were written first and watched failing. Gates: build-host.ps1
and run-checks.ps1.

## A check on the card: V9XD32 T20 ModAlpha

V9XD32's T20 gains a quad at x 344:
- grey behind at z 0.75;
- then T20's ARGB1555 DecalAlpha texture (left quadrants alpha 0, right
  alpha 1), MODULATE over white with vertex alpha 40h, alpha test
  GREATER 80h, at 0.5.

Direct3D takes the texel's alpha: the right quadrants draw red and green
and the left stay grey. The faulty modulate gives 1 x 40h for the right
and kills them too. The result is published as `T20ModAlphaOk`, outside
T20's verdict, because back ends with the same fault (the Matrox and
NV3 maps group MODULATE with MODULATEALPHA too, unmeasured) would
otherwise flip their release-check verdicts.

| Run | Hole (356,108) | Red (380,108) | Green (380,132) | `T20ModAlphaOk` |
|---|---|---|---|---|
| M-NZHWBR boot 45, before | 808080h | 808080h | 808080h | 0 |
| M-NZHWBR boot 46, 148af927 | 808080h | FF0000h | 00FF00h | 1 |
| W9QI0DAD Oland, 82dbf11e build | 808080h | FF0000h | 00FF00h | 1 |

On M-NZHWBR every other V9XD32 verdict and Ok key is identical before
and after, with `Result=PASS`. The Oland's run fails only on T19
FfSpecFog, as before.

Boot 45's driver set is backed up in the session scratchpad
(`nzhwbr-backup-boot45`).

## Not done

- Evergreen and Trinity: no card online. Their change is host-tested
  only.
- 3DMark 2000's helicopter on any of these: not re-run.
- The GT218 with the new check: not run (its Nature grabs are the
  evidence there).
