# The Rage Pro's second texture draws in one pass on the Rage XL, and Quake 2 runs at half the frame rate with it: not enabled

Date: 2026-10-07
Machine: A8U4I5, ATI Rage XL PCI (1002:4752), 800x600x16, boots 331-333.
Outcome: the composite works for unit 1 MODULATE, but Quake 2's
single-pass path is slower and sharper-edged than its two passes on this
machine. The code stays on branch `rage-pro-composite`; `main` is
unchanged and A8U4I5 is back on its 0.12.0 HAL (boot 333).
Driver: V9XDISP.DRV 0.12.0 (5904c9e-dirty) as installed; V9XHAL.DLL
b7baa40-dirty with this change (HAL only; `win9x_ddraw_abi.h` is unchanged
since 5904c9e). ICD 0.12.1 (927927b-dirty) as installed.
Instrument: `V9XGLP.EXE` (`tools/diag/gl_probe_win32.c`), its
GL_SGIS_multitexture section, and `V9XTRACE.EXE` snapshots either side.
Evidence: [`../probe/a8u4i5-rage-xl-composite-2026-10-07/`](../probe/a8u4i5-rage-xl-composite-2026-10-07/)

## Why

A forum post said the Rage Pro has single-pass multitexturing under
Direct3D 6 and OpenGL, and pointed at ATI's developer note, "Multitexturing
with the ATI Rage Pro" (web.archive.org, 2000-04-08 capture of
ati.com/na/pages/resource_centre/dev_rel/multex/). The note gives the pixel
pipeline as `AlphaBlend(Fog(Composite(tex1, tex2, diffuse) + specular))`
with four composite functions (modulate, modulate x2, linear blend by a
factor, add) and its caveats: both textures the same bit depth, paletted
pairs sharing one palette, no trilinear with two textures, both on the
same side of AGP, no D3D texture wrapping. It says nothing about
registers, and its OpenGL section is a placeholder.

The Mach64 path had always reported one texture unit, and the plan put two
units out of scope (`ati-rage-mobility-hardware-3d.md`, "Out of scope").
The Gateway (the plan's machine) was unreachable, so the measurement was
made on A8U4I5's Rage XL, the same Rage Pro class (`v9x_m64_chip_class`).

## Where the programming comes from

- Registers: xf86-video-mach64 `atiregs.h` (TEX_CNTL fields, the
  `VERTEX_n_SECONDARY_S/T/W` indices) and the Rage LT Pro register
  reference (`tmp/pdfs`, DP_PIX_WIDTH COMPOSITE_PIX_WIDTH, "the same width
  (in terms of bpp)"). Their TEX_CNTL clamp bits agree with the ones this
  driver measured (17, 18).
- Usage: Mesa 7.10's mach64 DRI driver (`mach64_texstate.c`,
  `mach64_tris.c`), the only driver whose source drove the composite. It
  sets `TEXTURE_COMPOSITE | SECONDARY_STW`, `COMP_COMBINE_MODULATE`, the
  second texture's size in TEX_SIZE_PITCH [27:16] and format in
  DP_PIX_WIDTH [7:4], `SECONDARY_TEX_OFF`, and with two textures
  SCALE_3D_CNTL `TEX_BLEND_FCN_TRILINEAR | TEX_CACHE_SPLIT`; it sends
  each vertex's secondary S, T and W (W = the vertex's own) after its six
  words. It drew only MODULATE on unit 1 and sent every other unit-1
  environment to software, and it ran every texture with
  `MIP_MAP_DISABLE`.

## The change

- `mach64_draw.c`: `v9x_m64_draw_composite` adds the second texture to a
  textured stream (one level, edge*2 bytes a row, on the base alignment,
  inside VRAM, off the target); the setup builders take
  `V9X_M64_SETUP_SECONDARY` and the register-reuse cache compares the
  secondary words.
- `mach64_policy.c`: a composite is accepted only with unit 1 MODULATE,
  unit 0 REPLACE or MODULATE (DECAL by texel alpha would lerp the product,
  not unit 0's colour), unit 1's own alpha only where nothing reads the
  fragment alpha (COMP_ALPHA is not set), and no fog or specular, which
  have no scene. Refusal reason 20 (`V9X_M64_REFUSE_COMPOSITE`); the shared
  counter arrays stop at 19, so it shows only as `M64PolicyLast`.
- `d3d_mach64_map.c`: unit 1 onto the request and state; unit 0's op from
  its color_op/alpha_op (a two-unit draw leaves `op` zero); unit 0 is
  sampled at level 0, at most bilinear, because the composite takes the
  blend function a chain or trilinear needs.
- `d3d_mach64.c`: `texture_units` 2, unit 1 resolved, its coordinates
  read from `texcoords1` and rebased by their own whole number when it
  wraps.
- Host tests first, watched failing: the composite's four state words
  against literal values, its refusals, the setup packet's order and
  register reuse with secondary words, the policy's boundary, and the map.

## Measured

Same probe, same machine; boot 331 on the old HAL, boot 332 on this one.

| | boot 331, old HAL | boot 332, composite HAL |
|---|---|---|
| GL_EXTENSIONS | empty | `GL_SGIS_multitexture` |
| SgisModulate (expect 100,100,13 +-8) | not run | 0x6B6508, Ok |
| SgisCoordLeft (expect black) | not run | 0x000000, Ok |
| SgisCoordRight (expect 200,100,50) | not run | 0xC66531, Ok |
| SgisUnit1Off | not run | 0xC66531, Ok |
| SgisReplace / Blend / Decal | not run | 0x000000 each, not Ok |
| ICD two-unit batches | 0 | 6 (`two-units=6/12`) |
| HAL M64Draws / M64TextureDraws over the run | 32 / 25 | 36 / 29 |
| HAL M64Refused (in draw) | 0 | 0 |

The four extra hardware draws are Modulate, CoordLeft, CoordRight and
Unit1Off; the first three are composites. CoordLeft and CoordRight differ
only in unit 1's S (0.25 against 0.75 over a half-black, half-white
texture), so the second texture reads its own coordinates through
`VERTEX_n_SECONDARY_S/T/W`, not unit 0's.

Unit1Off, a one-unit REPLACE of the same unit-0 texel, reads 198 red, the
same as CoordRight, so the composite with white adds no loss of its own
there. Modulate's red reads 107 where 565 inputs (206 x 132) predict 106.6;
the product's rounding is not settled, as item 9's blend rounding was not.

The three unit-1 modes the policy refuses (REPLACE, BLEND, DECAL) drew
nothing. The ICD logged each as refused, sent its CPU copy, and was refused
again: the Mach64 has no CPU fallback inside the render interface (Gen3
does). Before this change those programs were never offered the extension
and drew in two passes.

## Quake 2 on it

Quake 2 demo, `+set vid_ref gl +set vid_fullscreen 0 +set gl_mode 3
+set cheats 1 +map demo1`, `notarget`, `timerefresh` at the spawn, the
same boot (332) and HAL, `gl_ext_multitexture` 1 then 0. config.cfg was
saved first and put back after each run (identical both times).

| | `gl_ext_multitexture 1` (composite) | `gl_ext_multitexture 0` (two passes) |
|---|---|---|
| Console | `...using GL_SGIS_multitexture` | `...ignoring GL_SGIS_multitexture` |
| timerefresh, 128 frames | 17.07 s, 7.5 fps | 8.24 s, 15.5 fps |
| ICD batches / triangles, whole run | 181,572 / 1,680,601, of them two-unit 162,118 / 794,581 | 128,431 / 4,641,729 |
| ICD refused batches | 488 | 783 |
| Draws a frame during timerefresh (ICD tsc lines) | about 220 | about 80 |
| Time in the render interface a frame | about 96 ms | about 31 ms |

The frame rates are recorded to explain the decision, not as a
benchmark. The render-interface time per draw is about the same either
way, near 0.4 ms. What differs is the number of draws. Quake 2's
multitexture path binds a texture and a lightmap per surface, so a batch
ends whenever either changes, and batches average about 5 triangles
against about 36 in its texture-sorted two-pass path. On this machine the
per-draw cost dominates and the composite's saved fill does not show.

None of the refusals were two-unit draws: the only kind the ICD logged is
the one-unit texel-alpha-times-vertex-alpha blend already open
(`docs/issues/2026-10-03-rage-iic-texel-times-vertex-alpha-refused.md`).

The spawn view (`q2-mt1-spawn.png` against `q2-mt0-spawn.png`) shows the
cost of sampling unit 0 at level 0. The left wall's panels and the
ceiling's stripes, distinct with two passes, become texel noise with the
composite.

## What the evidence does not show, or disputes

- ATI's other three composite functions (modulate x2, linear blend, add)
  were not programmed. `COMP_COMBINE` takes more values than Mesa used;
  which value is which function is unmeasured.
- The note's "same bit depth" rule is met by construction here (every
  Mach64 texture is 16-bit); 8-bit paletted textures, which the note lists
  and the register reference confirms (DP_PIX_WIDTH 2), remain unused.
- Mip-mapping under a composite: unit 0 is sampled at level 0, as Mesa
  did. Whether level selection (MIP_MAP_DISABLE clear) still works with
  the composite holding the trilinear blend function was not tried; it
  is a separate bit, so it is a live hypothesis, not a known limit.
- Only the Rage XL ran it; the Gateway's Rage Mobility-M did not.
- Half-Life was not run: Quake 2, the program the composite would serve
  first, already decided it.

## Consequences

- Not enabled. Offering the extension on the Rage class would halve
  Quake 2 here, blur it with level-0 sampling, and leave the unit-1 modes
  the policy refuses (REPLACE, BLEND, DECAL) drawing nothing where the
  program used to draw in two passes. The Mach64 keeps one texture unit.
- The code is kept, host-tested, on branch `rage-pro-composite`. It is the
  starting point if either lever below changes the answer:
  - the render interface's cost per draw (about 0.4 ms here, both paths),
    which single-pass multitexture multiplies; and
  - mip level selection under the composite, unmeasured.
- Gates run on the branch: `build-host.ps1`, `check-tree.ps1` and
  `run-checks.ps1`, all passing.
