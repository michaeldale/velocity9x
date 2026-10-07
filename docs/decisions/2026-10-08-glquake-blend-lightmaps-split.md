# GLQuake's single-pass lightmaps reach the split on the Rage XL and draw as its own two passes do, at the same rate

Date: 2026-10-08
Machine: A8U4I5, ATI Rage XL PCI (1002:4752), 800x600x16, boots 344-345.
Driver: V9XDISP.DRV 0.12.0 as installed, the `main` HAL (composite) and
the `main` ICD with the split and its black-environment case (built from
c258976 plus d1c413f's change). The machine was left on its 0.12.0 HAL
and 0.12.1 ICD; `C:\QUAKE` stays installed.
Evidence: [`../probe/a8u4i5-rage-xl-glquake-2026-10-08/`](../probe/a8u4i5-rage-xl-glquake-2026-10-08/)

## Why

The ICD's split ([2026-10-08](2026-10-08-icd-two-unit-split.md)) had run
only on V9XGLP. A research pass for programs that set unit 1 to
something other than MODULATE named QuakeWorld. id's released source
shows GLQuake does the same. Both Windows `R_DrawSequentialPoly`s
(QW/client and WinQuake `gl_rsurf.c`, the `_WIN32` branch) draw
lightmapped surfaces single-pass with unit 0 REPLACE and unit 1
`GL_BLEND`, over `GL_LUMINANCE` lightmaps holding darkness, with
`GL_TEXTURE_ENV_COLOR` never set. That path is taken only with
`gl_texsort 0`; the default 1 draws texture-sorted chains in two passes
without multitexture. The Rage's composite has no BLEND, so every such
batch reaches the split. With a black environment colour that is
(ZERO, INVSRCCOLOR) alone (d1c413f).

The research prompt that started this said GLQuake used unit 1
MODULATE only. That was wrong: its line 452 sets GL_BLEND.

## Software

From the gamers.org idstuff mirror (`ftp.zx.net.nz`), with Michael's
go-ahead: `quake106.zip` (Quake 1.06 shareware; `id1/pak0.pak`,
MD5 5906e599...03abb, taken out of `resource.1` with `lha.py`, a
`-lh5-` decoder written for it), `glq1114.exe` (GLQuake v0.97, Quake
1.09; `glquake.exe` taken out of the installer's SZDD members with
`szdd.py`) and `qw230.exe` (QuakeWorld 2.30; `glqwcl.exe` copied but
not run, since the client needs a QuakeWorld server). Neither installer
was run. Their bundled `opengl32.dll`, 3Dfx's MiniGL, was left out: next
to the program it would replace the system's OpenGL and the ICD.

## Measured

`glquake.exe -window -width 640 -height 480 -condebug +gl_texsort N
+timedemo demo1`, 969 frames each.

| run | gl_texsort | multitexture | fps | ICD two-unit batches | split | declined | refused |
|---|---|---|---|---|---|---|---|
| mt0 | 0 | on | 21.6 | 41,772 | 41,772 | 0 | 41,800 |
| sort1 | 1 | on (unused) | 21.5 | 0 | 0 | 0 | 28 |
| mt0b | 0 | on | 21.4 | | | | |
| sort1b | 1 | on (unused) | 21.3 | | | | |
| nomt0 | 0 | `-nomtex` | 28.3, not comparable | 0 | 0 | 0 | 111,915 |

- Every two-unit batch was refused by the composite and by the CPU
  retry, then drawn by the split; none was declined. Without the split
  these were every lightmapped wall in that path, and they would have drawn
  nothing.
- The single-pass path drew at the rate of GLQuake's default path
  (21.6 and 21.4 against 21.5 and 21.3).
- Mid-demo frames of the two (`mt0b-gl_texsort0-multitexture.png`,
  `sort1b-gl_texsort1.png`, the same room a few frames apart) show the
  same textured, lit walls; nothing differs that the frame offset does
  not explain. Not a pixel comparison.
- The `-nomtex` run is faster because it draws less. Its 111,915
  refused batches are its lightmaps: 128x128 RGB565, REPLACE with the
  fragment's alpha, blended (SRCALPHA, INVSRCALPHA). The render
  interface has no Direct3D op for REPLACE that keeps the fragment's
  alpha, so the HAL and the CPU copy both refuse it. Filed as
  [`docs/issues/2026-10-08-mach64-replace-fragment-alpha-refused.md`](../issues/2026-10-08-mach64-replace-fragment-alpha-refused.md).
  It is an existing one-unit gap, not a result of the composite or the
  split.

## Gates

None run for this record beyond `check-tree.ps1`. It adds evidence and
two extraction scripts in the probe folder, and no driver change.
