# OpenGL display lists and texgen

Date: 2026-11-28. Status: steps 1-5 done 2026-11-28
([record](../decisions/2026-11-28-opengl-display-lists-and-texgen.md));
Minecraft not run, image commands in lists not compiled yet.

Two shipping games call entry points the ICD only stubs:

- Tux Racer 0.61a (report V9X-VK1M80, HD 4600): `glGenLists`,
  `glNewList`, `glEndList`, `glCallList`, `glTexGeni`, `glTexGenfv`.
- Minecraft 1.7 (report V9X-JVGQM2, HD 2500): `glGenLists`,
  `glNewList`, `glEndList`, `glCallLists`, `glDeleteLists`; no textured
  triangle was drawn.

Both are in [the issue](../issues/2026-11-15-tux-racer-display-lists-and-texgen-are-stubs.md);
the 1.0 plan made them the first OpenGL work for 1.1. The requirements
inventory ([opengl-1.1-requirements.md](opengl-1.1-requirements.md))
already names the modules: `gl_dlist.c` (slots 0-6) and `gl_texgen.c`
(slots 188-193), with `test_gl_dlist.c` and `test_gl_texgen.c`.

## Texgen (2.10.4)

`gl_texgen.c`, pure like `gl_light.c`: the mode and the object and eye
planes of S, T, R and Q; `glTexGen{i,f,d}{,v}` with their errors;
`glGetTexGen{i,f,d}v`; and the generated coordinates of one vertex.

- Eye planes are stored as given times the inverse of the model-view
  matrix current at the call (2.10.4), with a 4x4 inverse kept static in
  `gl_texgen.c`.
- The enables are the existing `TEXTURE_GEN_*` caps in `gl_state.c`,
  so push/pop of `ENABLE_BIT` already covers them.
- Applied in `v9x_gl_prim_vertex`: the vertex takes the generated
  coordinates, and the current texture coordinate (2.7) is left as it
  was. The enabled set is read at `glBegin`, which no enable can change
  before `glEnd`. Both the CPU path and the T&L capture take the
  vertex's `tex`, so both get it; vertex arrays reach the same function.
- One set of texgen state, applied to unit 0. Texgen per texture unit
  (ARB_multitexture) is not done: neither game needs it.
- Sphere maps use the eye-space normal through the inverse transpose of
  the model-view's upper 3x3, normalised only when `GL_NORMALIZE` is
  enabled, as 2.10.3 states.
- Not done: the texture matrix. The ICD applies no texture matrix
  today, with or without texgen.

## Display lists (5.4)

A list is a recording of GL calls, replayed through the same
implementations. The record is generated from the manifest, as the
dispatch table is, so no hand-kept copy of 336 prototypes can drift.

- **Where calls are caught:** the census forwarder that already sits in
  front of every implemented slot (`v9x_gl_install_census`). With
  `V9X_GL_DEFINE_SAVE`, `scripts\lib\gl-dispatch.ps1` emits a save step
  in each compilable slot's forwarder: while a list is being compiled,
  the call's arguments are copied into the list, and in `GL_COMPILE`
  mode it then returns without executing. One test of a global flag per
  call otherwise. Slots still holding their stub are not caught.
- **Node format:** one header word (slot, length in words), each scalar
  argument in one word (two for `GLdouble`), and each pointer argument
  as its byte count followed by a copy of what it points at.
- **Pointer sizes:** inferred from the name for the vector forms
  (`glColor3fv` is three floats); the rest are written in
  `src\opengl\gl_dlist_rules.psd1`, and generation fails for any pointer
  slot that has no rule.
- **Not compiled (5.4):** the commands the specification executes
  immediately (`glGenLists`, `glDeleteLists`, `glIsList`, the queries,
  `glFeedbackBuffer`, `glSelectBuffer`, `glRenderMode`, `glReadPixels`,
  `glPixelStore`, the client state and array pointers,
  `glGenTextures`, `glDeleteTextures`, `glAreTexturesResident`,
  `glIsTexture`, `glFlush`, `glFinish`), and `glNewList`/`glEndList`.
- **Vertex arrays in a list:** `glArrayElement`, `glDrawArrays` and
  `glDrawElements` take their data from the arrays when compiled (5.4).
  The node keeps a packed copy of each enabled array over the elements
  used (and the indices, rebased); replay points a temporary array state
  at the copy and calls the same draw, so Minecraft's chunks still reach
  the T&L path as one draw.
- **Compiled later, executed now for the first version:** the
  image-carrying commands (`glTexImage*`, `glTexSubImage*`,
  `glDrawPixels`, `glBitmap`, `glPolygonStipple`, `glPixelMap*`,
  `glMap*`, `glPrioritizeTextures`). Each is logged once, by slot, when
  a list catches it, so a field log says whether a game needs one.
- **Extensions** (`glActiveTextureARB`, `glMultiTexCoord*`, the SGIS and
  CVA entries) are reached through `wglGetProcAddress`, not the table:
  a call to one while compiling executes now and is logged once.
- **Names:** one table for the process, shared by every context. That
  is what `wglShareLists` asks for, and both games use one context.
- **Nesting:** `glCallList` inside a list is recorded as a call, not
  expanded; replay depth stops at 64 (`GL_MAX_LIST_NESTING`, the
  minimum 1.1 allows).
- **Queries:** `GL_LIST_BASE`, `GL_LIST_INDEX`, `GL_LIST_MODE`,
  `GL_MAX_LIST_NESTING`; `glPushAttrib(GL_LIST_BIT)` saves the base.

## Steps

1. Texgen: module, host test, ICD wiring. Gate.
2. Display lists: generator, rules, `gl_dlist.c`, host test, ICD
   wiring. Gate.
3. V9XGLP scenes for both, run on a fleet machine at 16 and 32 bpp.
4. Tux Racer on the i5-4570 (installed there), with a race frame
   compared against GDI Generic; Minecraft only if a copy can be had.
5. Record, README and changelog.
