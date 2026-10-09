# GLQuake draws white where its tiled background belongs beside the status bar

Date: 2026-10-09. Machine: the netbook (Intel GMA 950, Gen3 engine, wifi
at 10.0.1.254). Status: open, cause not investigated. Evidence:
`docs/probe/icd-census-attrib-2026-10-09/netbook-glquake-demo1.png`
(0.14.1 ICD, `b1002b0-dirty`) and
`docs/probe/netbook-multitexture-recheck-2026-10-08/glquake-gl_texsort0-multitexture-demo1.png`
(0.13.0 ICD).

## Symptom

At 640x480, GLQuake 0.97's status bar is 320 pixels wide and centred.
Left and right of it, from the status bar's top edge to the bottom of
the screen, the screen is plain white. GLQuake's quit screen showed white
around its dialog as well (agent screenshot, same session). Real GL fills
these areas with the tiled `backtile` texture. The world, the console,
the status bar and the viewmodel draw correctly in the same frames.

## What is known

- **It predates 0.14.1.** The 2026-10-08 recording on the 0.13.0 ICD,
  windowed, shows the same white areas. The fog, immediate-mode forms,
  census and attribute-stack changes of 2026-10-09 did not cause it.
- **One engine only.** It was seen on the GMA 950. Whether the software
  engine (86Box 9878) or the Rage XL do the same has not been checked.
- **Screenshots only.** Both images are GDI captures. The white has not
  been confirmed on the panel, but it sits exactly where the tile
  belongs and is stable across two captures and two builds.

## Hypotheses, none tested

id's `gl_draw.c` `Draw_TileClear` draws the area with `glBegin(GL_QUADS)`
and texture coordinates past 1, repeating a 64x64 texture. White would
follow if that texture is drawn as untextured with a white vertex colour.
That would happen if:

1. the texture is incomplete under GL 1.1 3.8.9 (for example a mipmapped
   minification filter with only level 0 uploaded), so the unit is
   disabled for the draw. That would be correct GL behaviour only if
   the application really left it incomplete;
2. the ICD's hardware copy or squared copy of a small texture is refused
   or mis-described, and the fallback drops the texture;
3. coordinates past 1 with REPEAT reach a path that refuses them.

The V9XGL.LOG `paths`, `refused` and `hwno` counters for a GLQuake
session, plus a probe scene that draws a 64x64 REPEAT texture with
coordinates past 1 and reads it back, would separate these.
