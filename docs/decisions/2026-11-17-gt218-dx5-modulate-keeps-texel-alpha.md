# GT218: DX5 MODULATE keeps the texel's alpha; Nature's trees keep their leaves

Date: 2026-11-17. Machine: M-BXMFA8 (Daniel's GeForce 210, GT218
`10DE:0A65`), driven over the relay; boot 142 before, boot 143 after.
Change: 448e2f2d (the Oland's fix, 82dbf11e, carried to the GT218).
Evidence: [`../probe/gt218-modulate-alpha-m-bxmfa8-2026-11-17/`](../probe/gt218-modulate-alpha-m-bxmfa8-2026-11-17/)
(grabs not committed: 3DMark's art).

## The cause, from the Oland

[2026-11-17-oland-dx5-modulate-keeps-texel-alpha.md](2026-11-17-oland-dx5-modulate-keeps-texel-alpha.md).
Nature's leaves are alpha-tested MODULATE draws on A8R8G8B8 textures
whose vertex alpha swings below one half. Direct3D takes the texel's
alpha alone; the back end multiplied the vertex's in, and the alpha test
then discarded the leaves. `d3d_nv50_map.c` gave MODULATE and
MODULATEALPHA one fragment variant in the same way.

## The change

A fourth op, `V9X_NV50_D3D_OP_MODULATE_RGB`: B, G and R multiplied, the
texel's alpha moved.
- Its four variants (specular x fog) start at code offset 4000h:
  reserve E5000h-E5400h, the gap after the scratch (which ends at the
  TIC table) and before the two-unit slots.
- The HAL places all sixteen variants. The channel probe, which has only
  the scratch, places the first twelve.
- The map picks it for MODULATE on A1, A4 or A8 formats when the core
  has not marked the stage's alpha as the diffuse's and an alpha test or
  a blend reads the alpha.

Host tests were written first and watched failing: the variant's words,
its placement in the gap, and the map's choices. Gates: build-host.ps1
and run-checks.ps1.

## On the card

The full nvidia set (DRV, VXD, HAL, SETP, GL) from this tree, by WININIT
rename, every file hash-checked after the restart. Boot 142's set is
backed up in the session scratchpad. 3DMark 2001 SE, Game 4 Nature
alone, `V9XTRACE -grab:20x10`, the same plan before and after:

| Grab | Boot 142 (before) | Boot 143 (after) |
|---|---|---|
| 2-3 (cottage, about 5 s and 13 s) | leafy | leafy |
| 4-5 (bridge 29 s, lake 25 s) | leafy | leafy |
| 6 (about 34 s) | bare trees | in leaf |
| 7 (about 43 s) | bare trees | in leaf |
| 8 (about 50 s) | bare tree against the sun | in leaf |

`EngineIdleTimeouts` 0. The run flipped 183 times, 4-12 fps.

## Seen, not changed

- **The lake** is flat grey-blue in both runs, with no reflection or
  ripples. That is a GT218 matter apart from this.
- **3DMark 2000's helicopter trees**
  ([issue](../issues/2026-11-16-gt218-3dmark2000-helicopter-trees-pop.md)):
  - not re-run;
  - Daniel saw the same popping on the HD 5450 (Evergreen) in October,
    only with 3DMark's "Pentium III" CPU optimisation and not with "D3D
    Software T&L" (chat, 2026-10-29). That fits vertex alpha as the
    variable;
  - Evergreen's and the R600 class's maps still fold MODULATE.
