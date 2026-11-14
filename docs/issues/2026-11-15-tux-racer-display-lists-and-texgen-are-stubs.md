# Tux Racer calls display lists and texgen, which the ICD only stubs

Date: 2026-11-15. Seen in report V9X-VK1M80 on 0.51.0 (`3d5a67b1`), an
HD 4600 (8086:0412) belonging to the tester of V9X-WM1C1K and
V9X-YP59WN, Windows 98 SE. The report has no description, so what the
tester saw on screen is not known. Status: open. Not reproduced here;
nothing is implemented.

## What the log shows

The Tux Racer 0.61a session in the report's `V9XGL.LOG` (pid FFF06233,
640x480, 16-bit depth, about 260 s) hits six slots the ICD has not
implemented. The first-call lines, in order:

```
stub first-call slot=192 glTexGeni
stub first-call slot=191 glTexGenfv
stub first-call slot=5 glGenLists
stub first-call slot=0 glNewList
stub first-call slot=1 glEndList
stub first-call slot=2 glCallList
```

At detach: `slots-called=6 stub-calls=312320`. The log counts stub calls
as a total, not per slot, so it does not say how the 312,320 split
between them. That total is about 1,200 calls a second, or about 66 a
frame at the session's steady 18 fps (182 swaps per 10 s).

The rest of the session is clean. 4.3 M triangles went through the
transform-and-lighting path with none refused or failed, 57 textures
were made, and the context was released normally.

## What a stub does

Every stub in the generated table records `GL_INVALID_OPERATION` and
returns an ABI-safe value (`gl_icd.c`). For this game, that means:

- `glGenLists` hands back no list. `glNewList` and `glEndList` do
  nothing, so the commands Tux puts between them run straight away,
  when the list would have been compiled. `glCallList` draws nothing.
  Whatever Tux keeps in a list is drawn once at build time, if at all,
  and never in a frame.
- `glTexGeni` and `glTexGenfv` are ignored. Geometry that relies on
  generated texture coordinates is drawn with whatever coordinates
  were last current.

This is reasoning from the stub contract. No frame has been captured.

## What Tux uses them for (not checked)

From memory of the 0.61 source, not checked against it here: the
penguin's body parts are spheres cached as display lists
(`hier_util.c`), and the course's terrain textures get object-linear
texgen coordinates (`course_render.c`). If that is right, Tux himself
and the terrain texturing are what goes missing or comes out wrong.
Confirm this from the source before relying on it.

## Where it sits in the plan

`docs/plans/opengl-1.1-requirements.md` puts display lists (slots 0-6,
287, `gl_dlist.c`) and texgen (slots 188-193, `gl_texgen.c`) in
Phase 6, with no evidence yet. `docs/STATUS.md` lists display lists as
not implemented. The 2026-10-23 and 2026-10-24 Tux Racer records note
both as stubs in passing. This record is the first to show a shipping
build calling them in the field.

## Next

1. Ask the tester what Tux Racer shows on 0.51.0.
2. Run Tux Racer 0.61a on the i5-4570 and capture a race frame
   against GDI Generic (`V9XGL.DLL` renamed aside), as in the
   2026-10-23 record.
3. Confirm what the 0.61 source keeps in lists and texgen, then
   implement Phase 6 display lists and texgen, test first
   (`test_gl_dlist.c`, `test_gl_texgen.c`).
