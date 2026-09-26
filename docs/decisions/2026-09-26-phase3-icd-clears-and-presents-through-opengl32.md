# Phase 3: the ICD clears and presents through the system OPENGL32 on the software engine

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 3 (the first V9XGLP gate on
86Box `Win98SE-Fast-D3D`: "the probe gets an ICD format; clear and swap
work")
Guest: 86Box `Win98SE-Fast-D3D` (port 9878, vbe package, `Direct3D=2`
software), boot 596, 1024x768x16 RGB565; V9XHAL.DLL from 37a17d3's tree;
V9XGL.DLL and V9XGLP.EXE from the tree this record is committed with;
`HKLM\...\OpenGLDrivers\Velocity9x=V9XGL.DLL` added with regedit /S.
Evidence: `2026-09-26-phase3-icd-clear-present-soft-V9XGLP.ini`,
`2026-09-26-phase3-icd-clear-present-soft-V9XGL.log`

## Measured

V9XGLP links OPENGL32 statically, as GLQuake does, and asks for GLQuake's
format (24 colour bits, 32 depth, double-buffered):

- OPENGL32 listed 25 formats; `ChoosePixelFormat` picked format 1, the
  ICD's: flags `0x425` (DRAW_TO_WINDOW, SUPPORT_OPENGL, DOUBLEBUFFER,
  SWAP_COPY), `ChosenGeneric=0`, 16 colour bits, 16 depth bits. The ICD
  described it from the HAL's `describe` (engine 1, target formats
  `1 << RGB565`), opened on the first `DrvDescribePixelFormat`.
- SetPixelFormat, wglCreateContext (through `DrvCreateLayerContext`, as
  Phase 0.9 measured) and wglMakeCurrent succeeded; the ICD made a 400x300
  drawable for the window.
- `glGetString`: "Velocity9x", "Velocity9x Software", "1.1.0", "".
- `glClearColor(1,0,1,1)`, `glClear(COLOR|DEPTH)`, SwapBuffers: no error,
  and the window's pixel read through GDI is `0x00FF00FF` - magenta on the
  screen.
- Scissor on, box over the lower-left quarter in window coordinates,
  green clear, swap: the pixel near the bottom-left is `0x0000FF00` and
  the one near the top-right is still magenta. The window-to-surface y
  flip and the scissor both reach the screen as specified.
- `glBegin`, not implemented yet: `glGetError` answers
  `GL_INVALID_OPERATION` (`0x502`) - a placeholder that says so, as the
  plan requires, not a silent success.
- Release and delete succeeded; the log's detach line counts one stub
  called once.

## Later the same day: geometry (Phase 4)

Evidence: `2026-09-26-phase4-icd-geometry-soft-V9XGLP.ini`, same guest and
boot, with the ICD built with `gl_matrix.c` and `gl_prim.c`. After the
clear checks above, V9XGLP sets an ortho projection onto the window,
enables the depth test with LESS, clears depth to 1, and in one
`glBegin(GL_QUADS)` draws a green quad at window depth 0.5 over
everything, a blue one at 0.75 over the left half and a red one at 0.25
over the right half. Read back after the swap: left `0x0000FF00` (green -
the blue quad rejected), right `0x000000FF` (red, COLORREF being BGR).
`glGetError` is clean after the geometry; `glLineWidth`, not implemented,
answers INVALID_OPERATION. Transform, clipping, the viewport and depth
mapping, the batches and the interface's depth test all reach the screen.

## Later still: textures, perspective-correct (Phase 4)

Evidence: `2026-09-26-phase4-icd-texture-soft-V9XGLP.ini`, boot 597 with
the HAL of `2026-09-26-phase3-explicit-draws-cpu-textures-scissor-and-
masks.md` and the ICD built with `gl_texture.c`. An 8x8 RGB texture, red
on its left half and blue on its right, NEAREST and REPLACE, on a quad
whose left edge is at z = -1 and right edge at z = -3 under
`glFrustum(-1,1,-1,1,1,10)`: the texture's s = 0.5 lies at ndc 0 when
interpolated with perspective and at ndc -1/3 when affine. Read back:
10% of the width red, 42% (ndc -1/6) **red** - perspective-correct - and
60% blue. glGenTextures, glBindTexture, glTexImage2D, glTexParameteri,
glTexEnvi and glDeleteTextures left no error. The texture reached the
software engine as CPU levels through the render interface, with q from
each triangle's rhw.

## And queries (Phase 4)

Evidence: `2026-09-26-phase4-icd-queries-soft-V9XGLP.ini`, same boot, the
ICD built with `gl_get.c`. glHint(PERSPECTIVE_CORRECTION_HINT, FASTEST),
glGetIntegerv(MAX_TEXTURE_SIZE) = 512 and glGetFloatv(MODELVIEW_MATRIX)
after glTranslatef(7,0,0) holding 7 in element 12, with no error; every
earlier check unchanged. The unimplemented-slot check now calls
glPushAttrib (Phase 6), since glLineWidth, which it used, is
implemented; it answers INVALID_OPERATION.

## And glReadPixels (Phase 5's first feature)

Evidence: `2026-09-26-phase4-icd-readpixels-soft-V9XGLP.ini`, same boot,
the ICD built with `gl_pixels.c`. After the scissored clear, glReadPixels
of one GL_RGB pixel at window (4, 4) returned green and at the top right
magenta - the same colours GDI read from the screen at the bottom left
and top right, so window rows count up from the surface's last row. After
the geometry scene the left and right pixels read green and red, matching
GetPixel. No error. The read follows the interface's finish with a
DirectDraw Lock of the back buffer, whose HAL side drains the engine; on
the software engine that drain has nothing to wait for, so the ordering
is not exercised here.

## Not established

- Gen3 and the ViRGE: the netbook is offline, and the ViRGE guest's 565
  desktop gets no ICD format by design (its generic formats would serve).
- The plan's other Phase 3 gate items: fullscreen, an overlapping window,
  100 create/destroy cycles against VRAM, two drawables, cross-thread
  rebind, resize/minimise/restore, a mode change, two GL processes and
  GL beside D3D.
- A timeout or device failure path: no clear failed.
- glReadPixels after hardware rendering, where the Lock's drain matters,
  and any format other than GL_RGB unsigned byte on a guest.

## Standing

Clear and present work end to end on the software engine through the
system runtime. Drawing geometry needs the Phase 4 pipeline.
