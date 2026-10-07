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
