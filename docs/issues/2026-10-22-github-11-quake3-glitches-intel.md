# Quake 3 demo glitches on Intel (GitHub issue 11)

Date: 2026-10-22. Reported by interrupt7 on the GMA 900 and the HD 4000,
0.23.0, Quake 3 demo 1.11, "Normal" preset (640x480, 16 bpp):
[GitHub issue 11](https://github.com/michaeldale/velocity9x/issues/11).
Status: one cause found and fixed in source on the HD 4600; the
reporter's two other symptoms are untested. Evidence:
[`../probe/hsw-4570-quake3-demo-2026-10-22/`](../probe/hsw-4570-quake3-demo-2026-10-22/).

## Reported

- On q3dm1, a wall emblem with a faint ghost of a neighbouring texture
  over it.
- The same emblem in a flat grey-white square, and a rectangle of floor
  grey-green instead of red-brown.
- The glitches look a little different on the two chips but are in the
  same places.
- On the HD 4000 at 32 bpp, glitching on the main menu when the mouse
  moves.

## Reproduced on the HD 4600 (boot 62, branch icd-32bpp-z24)

The demo installed at `C:\Q3Ademo` on the i5-4570 and run windowed at
640x480 on a 16 bpp desktop, `+devmap q3dm1`. Every run spawned at the
same point, `(212 2360 82) : -45` by `viewpos`.

**The floor rectangle reproduced.** For a reference, `V9XGL.DLL` was
renamed aside so OPENGL32 served Microsoft's "GDI Generic" software
renderer (`r_allowSoftwareGL 1`), at the same position. It drew the
floor plain. The difference of the two game screenshots (`F8`) is near
black except for the animated sky, the weapon, and a solid green
rectangle where the patch is.

**Census (the plan's step 1,
[`../plans/opengl-arb-multitexture.md`](../plans/opengl-arb-multitexture.md)):**
the renderer looked for `GL_S3_s3tc`, `GL_EXT_texture_env_add`,
`WGL_EXT_swap_control`, `GL_ARB_multitexture`,
`GL_EXT_compiled_vertex_array` and `WGL_3DFX_gamma_control`, and found
none. It drew with "multitexture: disabled" and "rendering primitives:
multiple glArrayElement". The reference ran the same way ("multitexture:
disabled" under GDI Generic). So the patch is not about which extensions
are offered.

**Cause.** V9XGL.LOG recorded one batch refused per frame with result 4
(UNSUPPORTED): two triangles, a hardware texture, depth LEQUAL without
writes, and blend `01090800`. That is source factor 9 and destination 8 in
D3D's numbering: `GL_DST_COLOR` / `GL_ONE_MINUS_DST_ALPHA`. The HAL counts
no refusals of its own for it. The pass was dropped, and the floor showed
without it.

No pixel format the ICD offers has alpha bits. GL 1.1 4.3.2 reads the
alpha of such a framebuffer as 1.0 ("If the framebuffer does not support
alpha values then the A that is obtained is 1.0"), and GDI Generic,
whose 16-bit format has none either, drew the pass that way. With
A_d = 1, `ONE_MINUS_DST_ALPHA` is `ZERO`, `DST_ALPHA` is `ONE`, and
`SRC_ALPHA_SATURATE`'s min(As, 1 - Ad) is 0.

**Fix.** The ICD now folds those three factors into what the engine is
handed (`v9x_gl_prim_no_dest_alpha` in `gl_prim.c`, host test first). The
application's factors are kept as set. After it, at the same position:
`hw-refused=0/0` (it was 2,133 batches over the earlier run), and the
difference image no longer shows the rectangle.

## Not tested

- **The wall emblem**, close up and in its grey square: not navigated
  to. The square may be the same refused factor on another surface; no
  other refusal was logged in the views run.
- **The GMA 900:** the fix is in the ICD, before any engine, so it
  applies there too, unmeasured.
- **The HD 4000's menu glitch at 32 bpp with the mouse:** a candidate is
  the blitter's early "idle" fixed in 7c02ad42
  ([2026-10-21-haswell-gl-present-at-32-bpp-half-width.md](2026-10-21-haswell-gl-present-at-32-bpp-half-width.md)),
  which the HD 4000 shares. Not run.
