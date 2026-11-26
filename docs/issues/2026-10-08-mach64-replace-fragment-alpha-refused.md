# Mach64 refuses REPLACE with the fragment's alpha, so GLQuake's -nomtex lightmaps draw nothing

Date: 2026-10-08. Machine: A8U4I5 (Rage XL PCI `1002:4752`). Status:
open. Evidence: `docs/decisions/2026-10-08-glquake-blend-lightmaps-split.md`
(`docs/probe/a8u4i5-rage-xl-glquake-2026-10-08/nomt0-*`).

## Symptom

GLQuake v0.97 with `-nomtex +gl_texsort 0` refused 111,915 batches in
`timedemo demo1` and ran faster (28.3 fps against 21.5) for drawing
less. The ICD logged them as 128x128 RGB565 textures (the converted
`GL_LUMINANCE` lightmaps), colour op REPLACE, alpha op FRAGMENT, blended
SRCALPHA / INVSRCALPHA with depth LESSEQUAL or GREATEREQUAL. The HAL's
passive check refused the CPU copy for its format (`M64AcceptPolicy07`)
after the surface texture was refused.

## Cause

`v9x_r3d_texture_op` (d3d_core.c) maps the render interface's combine to
a Direct3D texture op, and REPLACE colour with the fragment's alpha has
none: DECAL is REPLACE/REPLACE, and MODULATE keeps the fragment's alpha
only by multiplying the colour. The Mach64 policy works in Direct3D ops,
so it never sees the request. The chip has the operation: Phase 4 item
10 measured TEX_LIGHT_FCN REPLACE as C = Ct with A = Af for RGB565
(`mach64_policy.c`).

Not a regression: one-unit, and the same on 0.12.0. GLQuake's default
`gl_texsort 1` does not draw this way, and neither did Quake 2 or
Half-Life.

## Candidates, none measured

- On the Mach64, map RGB565 REPLACE+FRAGMENT to its REPLACE light
  function directly. It needs a combine the policy can read beside the
  Direct3D op, as the two-unit path's color_op/alpha_op already are.
- In the ICD, send such a draw as REPLACE/REPLACE when the texture has
  no alpha and the fragment's alpha is 1 across the batch (GLQuake's
  vertices are white), which makes the two the same.

## 2026-10-08, later: the same request on Gen3

The netbook (GMA 950) refuses the same 111,908 batches in the same run
(`docs/decisions/2026-10-08-netbook-multitexture-recheck.md`). Gen3 has a
software fallback, so they draw, but GLQuake's `-nomtex +gl_texsort 0`
timedemo then runs at 3.5 fps against 52.8 for its default path. The gap
is in the core's Direct3D-op mapping, not one engine, so the title's
"Mach64" is narrower than the defect.

## Status 2026-11-08 (audit)

Since 61b58b8a (2026-10-24) the ICD rewrites REPLACE/FRAGMENT on RGB565 as MODULATE with white vertex colours that keep their alpha. On the i5-4570 this cleared Tux Racer's refusals and V9XGLP's ReplaceRgbBlend checks. GLQuake `-nomtex +gl_texsort 0` has not been re-run on the Rage XL or the GMA 950. The core's Direct3D-op mapping is unchanged.

## 1.0 re-read, 2026-11-26

Re-run owed on the release candidate (the 1.0 plan's re-run list); if
the run does not settle it, it becomes a README known issue. The
MODULATE rewrite (61b58b8a) is measured on the i5-4570 only; GLQuake
-nomtex on the Rage XL and the GMA 950 is in the plan's re-run list.

## 2026-11-26: GMA 950 re-run, nothing refused, the picture wrong

GLQuake `-nomtex +gl_texsort 0 +timedemo demo1` on the netbook (released
0.60.0): 0 of 1,256,219 batches refused, 43.9 fps. But the screen shows
the world as grey lighting only, with no wall or floor texture; the same
demo with multitexture draws textured, lit walls. The ICD counts
`alpha-dropped=23572`. Reading, not measured: the RGBA lightmaps lose
their alpha as RGB565, so the rewritten pass covers the base texture
instead of darkening it. Stays open as a README known issue (GMA 950,
GLQuake `-nomtex` only); the Rage XL run is still owed in the A8U4I5
round ([record](../decisions/2026-11-26-1.0-reruns-gt218-netbook-gm965.md)).

Not pursued for 1.0 (2026-11-26): the fault needs both `-nomtex` and
`gl_texsort 0`; GLQuake's default `gl_texsort 1` draws the same room
correctly (the 2026-10-08 table). The README names it; finding the
cause is listed for 1.1 in the 1.0 plan.
