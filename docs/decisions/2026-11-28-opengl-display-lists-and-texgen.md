# OpenGL display lists and texgen: Tux Racer's terrain and dial drawn

Date: 2026-11-28

Plan: [opengl-display-lists-and-texgen.md](../plans/opengl-display-lists-and-texgen.md).
Issue: [2026-11-15-tux-racer-display-lists-and-texgen-are-stubs.md](../issues/2026-11-15-tux-racer-display-lists-and-texgen-are-stubs.md)
(reports V9X-VK1M80, Tux Racer on an HD 4600, and V9X-JVGQM2,
Minecraft on an HD 2500).

Evidence: [`../probe/gl-dlist-texgen-2026-11-28/`](../probe/gl-dlist-texgen-2026-11-28/).

## What was built

- **Texgen** (`gl_texgen.c`, 2.10.4): `glTexGen{i,f,d}{,v}` and
  `glGetTexGen{i,f,d}v`; OBJECT_LINEAR, EYE_LINEAR (the plane taken
  through the inverse of the model-view at the call) and SPHERE_MAP (the
  eye-space normal through the inverse transpose, normalised only under
  `GL_NORMALIZE`). Applied in `v9x_gl_prim_vertex` to the one vertex, so
  the CPU path and the T&L capture both take it, vertex arrays included;
  the current texture coordinate is left as it was. One set of state for
  unit 0.
- **Display lists** (`gl_dlist.c`, 5.4): names, compile, replay,
  nesting to 64, `glCallLists` in every type with `glListBase`, and the
  `GL_LIST_BASE`, `GL_LIST_INDEX` and `GL_LIST_MODE` queries. A call is
  caught in the census forwarder that already stands in front of every
  implemented slot. `scripts\lib\gl-dispatch.ps1` now generates, from
  `gl_entrypoints.psd1` and the new `gl_dlist_rules.psd1`, a save
  function for each compiled slot (scalar arguments and a copy of what
  each pointer points at) and the replay switch that calls the slot's
  implementation with them. Generation fails for a compiled slot whose
  pointer has no size rule.
- `glArrayElement`, `glDrawArrays` and `glDrawElements` in a list copy
  the enabled arrays over the elements used (and the indices, rebased);
  replay points the arrays at the copy for one draw.
- **Not compiled yet:** the image commands (`glTexImage*`,
  `glTexSubImage*`, `glDrawPixels`, `glBitmap`, `glPolygonStipple`,
  `glPixelMap*`, `glMap*`, `glPrioritizeTextures`) and the multitexture
  extension entries run when called and are logged once each
  (`dlist executed-not-compiled`). Push/pop of `GL_LIST_BIT` and of
  texgen's planes under `GL_TEXTURE_BIT` is not done; the texgen enables
  are pushed with the other enables, as before.

New external functions: the `v9x_gl_texgen_*` and `v9x_gl_dlists_*`
APIs of the two new modules, used only by `gl_prim.c`, `gl_icd.c` and
the host tests, as the other `gl_*.c` modules' are.

## Host tests

`test_gl_texgen.c`: initial values, errors, each mode against values
worked from 2.10.4's formulas (sphere maps in double precision, with and
without `GL_NORMALIZE` under a scaled model-view). `test_gl_prim.c`
gains a texgen triangle through the pipeline. `test_gl_dlist.c`: the
name table (5,000 names, deletes, a program's own names), compile-only
and compile-and-execute, copies of pointer arguments, doubles and
negative shorts round-tripped, glCallLists with a base, nesting stopped
at 64, Deferred and Execute slots, 2,000 lists, and a node past the
header's 16-bit length. `build-host`, `check-tree` and `run-checks`
pass.

## Measured: V9XGLP on the GeForce 9500 GT

M-Z2T02D (GitHub issue 30), boot 13, 1024x768x32, the `3a1335ec`
nvidia set with only `V9XGL.DLL` replaced (build `e43af441-dirty`; the
previous one kept as `C:\V9XDIAG\G96\V9XGL.OLD`). New probe scenes:

| Key | Read | Expected |
|---|---|---|
| `ListCompileOnly` (before glCallList) | 000000 | the clear: GL_COMPILE ran nothing |
| `ListQuad` | FF0000 | red |
| `ListNested` (translated, calling the first) | FF0000 | red |
| `ListArrays` (array turned blue and disabled after compiling) | 00FF00 | green |
| `ListCallLists` (with glListBase) | FF0000 | red |
| `ListIsList`, `ListDeleted` | 1, 1 | |
| `TexGenObj0-3`, `TexGenEye0-3` | FF0000, 00FF00, 0000FF, FFFFFF | the strip's four columns |
| `ErrorAfterLists`, `ErrorAfterTexGen` | 0 | |

Every other key of the report matches the run of the same morning on
the previous ICD. The log: `counters dlist compiles=3 calls=5
array-draws=1 not-compiled=0`. The Quake 3 demo (no lists) ran demo001
at 43.8 fps against 44.1 that morning.

## Measured: Tux Racer on the HD 4600

The i5-4570 (W9QI5A83), boot 300, 800x600 desktop on its 1.0.0 set
(`82112c9c`), Tux Racer 0.61a switched to windowed in
`config\options.txt` so the agent's screenshot sees its frames (the
original kept as `OPTIONS.ORG` and put back after). The same practice
race, Frozen River, about 4 and 6 seconds in, on each ICD:

| | 1.0.0 ICD (`82112c9c`) | this ICD (`e43af441-dirty`) |
|---|---|---|
| Terrain and rock ridge | flat white and grey, untextured | snow and rock textures |
| Speed dial | missing, only the digits | drawn |
| Tux, trees, flags, sky, HUD text | drawn | drawn |
| `stub first-call` lines | glTexGeni, glTexGenfv, glGenLists, glNewList, glEndList, glCallList | none |
| Swaps per 10 s | 166-181 | 173 |

This ICD's log for the race: `counters dlist compiles=4 calls=18292
not-compiled=0`; `tnl batches=25549 triangles=1977115 refused=0
failed=0`; 38 textures made, none failed. Tux was ended with the
agent's `kill`, and the 4570 left on its 1.0.0 ICD with its options
file restored (`V9XGL.PRE` beside it is the same file).

The issue's guess was that Tux's body is held in lists. It is drawn on
both ICDs, so it is not, or not only. Which of the two features brings
back the terrain and which the dial was not separated (one ICD has both
or neither); the issue's reading of the source, texgen for the course,
fits the terrain.

## Not run

Minecraft (no copy here), so V9X-JVGQM2's case (lists of vertex-array
chunks and `glCallLists` of unsigned ints) is covered by the host tests
and V9XGLP's `ListArrays` only. Sphere maps on hardware. Tux on other
chips: the change is in the ICD core, shared by every engine.
