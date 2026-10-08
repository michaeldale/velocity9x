# The GMA 950 on today's ICD: the split never runs, the level probe is exact, GLQuake uses Gen3's BLEND, and Half-Life OpenGL at 640x480 holds 17.1-17.6 fps

Date: 2026-10-08
Machine: MICHAEL-NETBOOK (945GSE / GMA 950), boot 119, 1024x576x16,
wifi 10.0.1.254. Installed set: the 2026-10-07 Gen3-clip build
(`nb-gen3-clip`), HAL unchanged. Only V9XGL.DLL was replaced, by the
`main` ICD with the split and its black-environment case (built from
c258976 plus d1c413f's change), and put back afterwards.
Evidence: [`../probe/netbook-multitexture-recheck-2026-10-08/`](../probe/netbook-multitexture-recheck-2026-10-08/)

## Why

Today's ICD changes reach every engine: `v9x_gl_draw_into` takes the
batch as a parameter, and `v9x_gl_draw_split` runs wherever a two-unit
batch is refused whole and with its CPU copy
([2026-10-08](2026-10-08-icd-two-unit-split.md)). On Gen3 a refused
batch goes to the software fallback, so the split should never run;
that needed checking. V9XGLP's new level-tagged section had not run on
Gen3's two-unit program. GLQuake's `gl_texsort 0` path is the first
game to use unit 1 GL_BLEND
([2026-10-08](2026-10-08-glquake-blend-lightmaps-split.md)), which Gen3
draws itself. Michael asked for Half-Life's OpenGL figure at 640x480.

## Measured

V9XGLP (`V9XGLP.INI`): `Result=PASS`, every SGIS case in tolerance
(Blend 0x630063, one pass, against the Rage's two-pass 0x5A0063), and
every SgisMip case on its level, the perspective ones with unit 1 on
included. ICD: 36 two-unit batches, none refused, none split.

Recorded as measured; none is a target.

| | runs | fps | ICD two-unit batches | refused | split |
|---|---|---|---|---|---|
| Quake 2 demo1 spawn `timerefresh`, 640x480 window, multitexture | 1 | 34.5 | 571,042 | 0 | 0 |
| the same, `gl_ext_multitexture 0` | 1 | 32.4 | 0 | 0 | 0 |
| GLQuake v0.97 `timedemo demo1`, 640x480 window, `gl_texsort 0` (unit 1 BLEND) | 2 | 59.7, 59.8 | 41,740 | 20 | 0 |
| GLQuake, default `gl_texsort 1` (its own two passes) | 1 | 52.8 | 0 | 20 | 0 |
| GLQuake, `-nomtex +gl_texsort 0` | 1 | 3.5 | 0 | 111,908 | 0 |
| Half-Life 1.1.1.0 `-gl`, 640x480 fullscreen (its saved mode), `timedemo mwd5` | warm-up + 2 | 16.77; 17.14, 17.59 | 68,793 | 0 | 0 |

- **The split never ran** on Gen3 in any of these. Every two-unit batch
  drew on the GPU.
- **Quake 2** keeps the order the 2026-10-05 record's last figures had
  (34.92 against 30.40): multitexture ahead. The two spawn frames are
  the same.
- **GLQuake** draws its single-pass lightmaps through Gen3's own unit-1
  BLEND, 13% faster than its default two-pass path on this machine. A
  mid-demo frame (`glquake-gl_texsort0-multitexture-demo1.png`, a third
  run, 57.1 fps with the screenshot taken during it) shows textured, lit
  walls. The default path's run has no mid-demo frame; the comparison is
  of rates, not pictures.
- **GLQuake `-nomtex +gl_texsort 0`** refuses the same 111,908 lightmap
  batches as on the Rage XL (REPLACE with the fragment's alpha). Gen3
  hands them to its software fallback, so they draw, at 3.5 fps. See
  the issue below.
- **Half-Life** at 640x480 fullscreen OpenGL: 17.14 and 17.59 fps, where
  the 2026-10-05 record's last measurement on the same mode had 17.64
  and 17.65. No batch refused. The mid-demo frame
  (`hl-mwd5-640x480.png`) is drawn correctly.

## What this does not show

- Half-Life with and without the extension today: it ignores `-nomtex`,
  and no ICD built without the extension was run this time. The
  2026-10-05 record has them level (17.64, 17.65 against 17.63, 17.40).
- A pixel comparison of GLQuake's two paths on Gen3.

Gates: none for this record beyond `check-tree.ps1`; it changes no code.
