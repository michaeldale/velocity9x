# Haswell, a tester's HD 4600 (report V9X-YP59WN): Tux Racer's sky and fogged textures never drawn

Date: 2026-10-24. Reported on 0.28.0 (`8e6b7bd6`) by the tester of
V9X-D9VZS6, V9X-D9VC83 and V9X-WM1C1K: "textures and skybox don't render
properly in tuxracer, stutters in all tested opengl games, artifacts on
car wheels remain in revolt". Status: the Tux Racer sky is reproduced on
the i5-4570 (W9QI5A83, boot 75), fixed in the ICD, and seen fixed there
in a race. The stutter and Re-Volt are not addressed here. Evidence:
[`../probe/hsw-gl-replace-rgb-2026-10-24/`](../probe/hsw-gl-replace-rgb-2026-10-24/).

## What the tester's log showed

In a Tux Racer race, batches on textures 55 (512x512), 56, 57 and 60
were logged as `refused` with the key `tex=02010100`: a surface
texture, RGB565, colour op REPLACE, alpha op FRAGMENT. Blending was
SRC_ALPHA/INV_SRC_ALPHA and fog was on. Each was followed by `draw
result=4` (UNSUPPORTED). By the end, `failed r4=3088` batches and
`fog-dropped=11869` triangles: nothing of them was drawn. Full-screen
triangles on a 512x512 texture fit the sky.

## Cause

GL_REPLACE on an RGB texture is the texel's colour with the fragment's
alpha (GL 1.1 table 3.18). The ICD sends that as REPLACE/FRAGMENT. For a
one-unit surface texture the Direct3D core turns the ops into a Direct3D
texture op (`v9x_r3d_texture_op`, `d3d_core.c`), and none of those is
"texel colour, fragment alpha", so it returns UNSUPPORTED before any
engine sees the draw. The Haswell engine could draw it (its two-unit map
has `V9X_HSW_PS_DECAL`, Ct with Af), but the one-unit path never asks.

The ICD rewrites REPLACE/FRAGMENT to REPLACE/REPLACE only when nothing
reads the result's alpha (`v9x_gl_tex_fragment_alpha_unused`). Tux's
blend reads it, so the rewrite did not apply. Then both retries failed:
the CPU copy went to the software fallback and was refused there too.
The fog-dropped retry resent the same surface form, which was refused
again. Why the fallback refused the unfogged probe batch on Haswell
(below) was not traced; on the netbook's Gen3 the fallback drew it.

## Fix

The ICD now rewrites a one-unit surface texture's REPLACE/FRAGMENT on
RGB565 as MODULATE, with each vertex's colour set to white and its alpha
kept (`v9x_gl_tex_replace_as_modulate`, `gl_texture.c`). MODULATE by
white is the texel's colour exactly, and MODULATE beside the fragment's
alpha on a texture without alpha is Direct3D's MODULATE, which every
one-unit engine has. This is the rewrite the CIF wrapper already makes
(`cif_state.c`) and the Glide wrapper's equivalent for its own case
(`glide_state.c`). The CPU copy is left alone: the software rasterizer
takes REPLACE/FRAGMENT as it is.

Host test first (`test_replace_as_modulate`, `test_gl_texture.c`), seen
failing against a stub (13 checks) and passing after. `build-host`,
`check-tree` and `run-checks` pass.

## Probe

V9XGLP gains `ReplaceRgbBlend` and `ReplaceRgbFogBlend`: a green RGB
texture under GL_REPLACE, a blue vertex of alpha 0.5, SRC_ALPHA blended
over black, read back with `glReadPixels`. Expected green 128; then with
LINEAR red fog at f = 0.5, red 64 and green 64. A blue cast would mean
the vertex colour leaked into the result, and full green would mean the
texel's alpha was used instead of the fragment's.

| Machine, ICD | ReplaceRgbBlend | ReplaceRgbFogBlend | Log |
|---|---|---|---|
| 4570, `b09a5d77-dirty` (before) | `0x000000`, fail | `0x000000`, fail | `refused tex=26 ... tex=02010100`, `draw result=4` twice, `failed r4=2` |
| 4570, `a19476c9-dirty` (this fix) | `0x008200`, ok | `0x424100`, ok | no refusal for texture 26, `failed r4=0` |
| netbook (Gen3), `44527aec-dirty` (before) | `0x008200`, ok | `0x394100`, ok | the same `refused` line, then drawn by the software fallback, `failed r4=0` |

The probe's overall `Result` does not count these keys, like its fog
keys.

## Tux Racer on the 4570

Tux Racer 0.61a, 640x480 full screen, desktop 800x600x16, the ICD alone
replaced (the previous one kept as `C:\WINDOWS\SYSTEM\V9XGL.B09`).
Driven blind: the agent's GDI screenshot is black while Tux presents
through GL. The way in, by relative mouse moves from the top-left
corner: click (320,260) twice (splash, then Practice), then Race! at
about (433,418).

In the race, textures 55 and 60 are drawn with the tester's refused
state (`tex=01010100 ... fog=1`), with no `refused` or `draw result`
line in the process. Over about 100 s of race: `hw-refused=0/0`,
`fog-dropped=0`, `failed r4=0`. Tux was ended with `C:\WKILL.EXE` and
the desktop reset with `V9XMSW /set:800x600x16`.

The picture itself was not seen. The log says the batches were drawn;
whether the sky looks right is not checked.

## Not addressed

- The race runs at about 15.5 frames a second (154-156 swaps per 10 s)
  with 10-11 % of wall time in the driver's draws. The tester saw about
  16. Lighting, materials, texgen and display lists are still stubs (3.8
  million stub calls in this race).
- The stutter in the other GL games, and Re-Volt's wheels.
