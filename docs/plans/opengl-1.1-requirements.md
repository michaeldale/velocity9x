# OpenGL 1.1 requirements inventory

Status: Open. Generated 2026-09-26 from `src/opengl/gl_entrypoints.psd1` by
`scripts/generate-opengl-requirements.ps1`; the cross-cutting section is written by hand there. This
is the inventory `docs/plans/opengl-1.1-icd.md` asks for in Phase 0: one row
per dispatch slot, plus the requirements no single entry point owns.

Section numbers are the OpenGL 1.1 specification's
(<https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf>). Phase and
module are the plan's. The test column names the host test or probe scene
that is *planned* to cover the row. **Evidence is `none` for every row**
until a gate records otherwise; a slot that merely exists and returns is
not evidence, and the plan says so. Update this file when a row gains
evidence, naming the decision doc that holds it.

Counts by owning phase: phase 4 = 176; phase 4, completed in 6 = 21; phase 5, completed in 6 = 1; phase 6 = 138; total 336.

## Cross-cutting requirements

These are not owned by one entry point. Each has to be closed with its own
evidence before the Phase 6 gate.

| Requirement | Specification | Phase | Planned evidence | Evidence |
|---|---|---|---|---|
| Error semantics: first error is retained, `GL_INVALID_OPERATION` inside Begin/End, `GL_INVALID_ENUM`/`GL_INVALID_VALUE` per call, `GL_OUT_OF_MEMORY` behaviour, and no error from a temporary stub being mistaken for conformance | 2.5 | 4, completed in 6 | `test_gl_state.c` legal/illegal call tables; the stub tracker below | none |
| Default state for every state variable in the state tables | 6.2 tables 6.5-6.29 | 4, completed in 6 | `test_gl_get.c` asserts every default after context creation | none |
| Implementation limits (`GL_MAX_*`, `GL_SUBPIXEL_BITS`, stack depths, `GL_MAX_TEXTURE_SIZE`) describe the implementation including software, not one engine | 6.2 table 6.29 | 4 | `test_gl_get.c`; the advertised texture limit is honoured on every engine through software | none |
| Primitive assembly for all ten `glBegin` modes, provoking vertex (last; first for `GL_POLYGON`), edge flags, invalid vertices between Begin/End | 2.6 | 4 | `test_gl_prim.c` | none |
| Vertex transformation, clip-space clipping against six frustum and six user planes, perspective divide, viewport and reversed depth range | 2.10-2.11 | 4 | `test_gl_clip.c`, `test_gl_xform.c`; reversed range scene against generic GL | none |
| Rasterization contract: pixel centres, lower-left origin versus surface rows, shared-edge coverage, winding after Y flip, scissor | 3.1, 3.5.1, 4.1.2 | 2, 4 | Phase 2 contract doc; adjacent-triangle and boundary pixel tests in `test_d3d_raster.c` and `test_gl_scene.c` | none |
| Perspective-correct interpolation of colour, depth, fog and homogeneous texture coordinates including varying q | 3.5.1 | 2 | projective texture through clipping scene; `test_d3d_raster.c` | none |
| Point and line rasterization: coverage, endpoint rules, connected blended segments, width, stipple, smoothing | 3.3-3.4 | 2, 6 | `test_r3d_line.c`, `test_gl_scene.c` | none |
| Texture completeness, mip chain to 1x1, non-square images, borders, all six minification filters, wrap modes, environment functions, alpha decode | 3.8 | 2, 4 | `test_gl_teximage.c`, `test_gl_texobj.c`, `test_d3d_raster.c` | none |
| Logical texture images survive surface loss, eviction and engine refusal; no silent downscale or truncation | 3.8.8 | 4 | `test_gl_texobj.c`; V9XGLP loss/restore scene | none |
| Pixel pack/unpack: alignment, row length, skip pixels/rows, all format/type pairs, orientation | 3.6.1, 4.3.2 | 4, completed in 6 | `test_gl_pixelstore.c`, `test_gl_pixel.c` | none |
| Per-fragment order: scissor, alpha, stencil, depth, blend, logic op, masks, dithering | 4.1-4.2 | 2, 4, 6 | `test_d3d_raster.c` order tests; `test_gl_scene.c` | none |
| Front and back buffer selection for drawing and reading, single-buffered formats, behaviour when stencil/accum/alpha planes are absent | 4.2.1, 4.3, 6.1.1 | 3, 6 | V9XGLP front/back scenes; `test_gl_get.c` for absent-buffer queries | none |
| Context and drawable lifetime: one current thread per context, rebind, delete while current, share groups, repeated create/destroy, `FreeLibrary` of opengl32 | WGL contract | 3, 4, 6 | V9XGLP lifetime scenes; Quake 2 `vid_restart` | none |
| Display list compile/execute semantics, `GL_COMPILE_AND_EXECUTE`, non-listable commands executed immediately, shared lists | 5.4 | 6 | `test_gl_dlist.c` | none |
| Attribute stack groups push and pop exactly the state the tables assign to them | 6.1.11 table 6.30 | 6 | `test_gl_state.c` | none |
| `DrvGetProcAddress` returns NULL for every name not implemented; `GL_EXTENSIONS` under 4 KB; vendor/renderer strings avoid the Quake substrings | ICD contract; Quake census | 3 | `test_gl_get.c`; V9XGLP string check | none |
| Temporary stubs: every slot whose phase has not landed sets `GL_INVALID_OPERATION` with an ABI-safe return; the list of such slots is kept here and must be empty at the Phase 6 gate | plan Phase 3 | 3-6 | the generated stub list, checked by `test_gl_dispatch.c` | none; stub list not yet generated |

## Entry points

| Slot | Entry point | Specification | Phase | Module | Planned test | Evidence |
|---|---|---|---|---|---|---|
| 0 | `glNewList` | 5.4 Display lists | 6 | `gl_dlist.c` | `test_gl_dlist.c` | none |
| 1 | `glEndList` | 5.4 Display lists | 6 | `gl_dlist.c` | `test_gl_dlist.c` | none |
| 2 | `glCallList` | 5.4 Display lists | 6 | `gl_dlist.c` | `test_gl_dlist.c` | none |
| 3 | `glCallLists` | 5.4 Display lists | 6 | `gl_dlist.c` | `test_gl_dlist.c` | none |
| 4 | `glDeleteLists` | 5.4 Display lists | 6 | `gl_dlist.c` | `test_gl_dlist.c` | none |
| 5 | `glGenLists` | 5.4 Display lists | 6 | `gl_dlist.c` | `test_gl_dlist.c` | none |
| 6 | `glListBase` | 5.4 Display lists | 6 | `gl_dlist.c` | `test_gl_dlist.c` | none |
| 7 | `glBegin` | 2.6 Begin/End paradigm | 4 | `gl_prim.c` | `test_gl_prim.c` | none |
| 8 | `glBitmap` | 3.7 Bitmaps | 6 | `gl_pixel.c` | `test_gl_pixel.c` | none |
| 9 | `glColor3b` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 10 | `glColor3bv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 11 | `glColor3d` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 12 | `glColor3dv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 13 | `glColor3f` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 14 | `glColor3fv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 15 | `glColor3i` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 16 | `glColor3iv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 17 | `glColor3s` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 18 | `glColor3sv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 19 | `glColor3ub` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 20 | `glColor3ubv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 21 | `glColor3ui` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 22 | `glColor3uiv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 23 | `glColor3us` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 24 | `glColor3usv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 25 | `glColor4b` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 26 | `glColor4bv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 27 | `glColor4d` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 28 | `glColor4dv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 29 | `glColor4f` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 30 | `glColor4fv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 31 | `glColor4i` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 32 | `glColor4iv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 33 | `glColor4s` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 34 | `glColor4sv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 35 | `glColor4ub` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 36 | `glColor4ubv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 37 | `glColor4ui` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 38 | `glColor4uiv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 39 | `glColor4us` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 40 | `glColor4usv` | 2.7 Vertex specification (colours) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 41 | `glEdgeFlag` | 2.6.2 Polygon edges | 6 | `gl_prim.c` | `test_gl_prim.c` | none |
| 42 | `glEdgeFlagv` | 2.6.2 Polygon edges | 6 | `gl_prim.c` | `test_gl_prim.c` | none |
| 43 | `glEnd` | 2.6 Begin/End paradigm | 4 | `gl_prim.c` | `test_gl_prim.c` | none |
| 44 | `glIndexd` | 2.7 Vertex specification (colour index) | 6 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 45 | `glIndexdv` | 2.7 Vertex specification (colour index) | 6 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 46 | `glIndexf` | 2.7 Vertex specification (colour index) | 6 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 47 | `glIndexfv` | 2.7 Vertex specification (colour index) | 6 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 48 | `glIndexi` | 2.7 Vertex specification (colour index) | 6 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 49 | `glIndexiv` | 2.7 Vertex specification (colour index) | 6 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 50 | `glIndexs` | 2.7 Vertex specification (colour index) | 6 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 51 | `glIndexsv` | 2.7 Vertex specification (colour index) | 6 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 52 | `glNormal3b` | 2.7 Vertex specification (normals) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 53 | `glNormal3bv` | 2.7 Vertex specification (normals) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 54 | `glNormal3d` | 2.7 Vertex specification (normals) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 55 | `glNormal3dv` | 2.7 Vertex specification (normals) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 56 | `glNormal3f` | 2.7 Vertex specification (normals) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 57 | `glNormal3fv` | 2.7 Vertex specification (normals) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 58 | `glNormal3i` | 2.7 Vertex specification (normals) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 59 | `glNormal3iv` | 2.7 Vertex specification (normals) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 60 | `glNormal3s` | 2.7 Vertex specification (normals) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 61 | `glNormal3sv` | 2.7 Vertex specification (normals) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 62 | `glRasterPos2d` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 63 | `glRasterPos2dv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 64 | `glRasterPos2f` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 65 | `glRasterPos2fv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 66 | `glRasterPos2i` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 67 | `glRasterPos2iv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 68 | `glRasterPos2s` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 69 | `glRasterPos2sv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 70 | `glRasterPos3d` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 71 | `glRasterPos3dv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 72 | `glRasterPos3f` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 73 | `glRasterPos3fv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 74 | `glRasterPos3i` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 75 | `glRasterPos3iv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 76 | `glRasterPos3s` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 77 | `glRasterPos3sv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 78 | `glRasterPos4d` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 79 | `glRasterPos4dv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 80 | `glRasterPos4f` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 81 | `glRasterPos4fv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 82 | `glRasterPos4i` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 83 | `glRasterPos4iv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 84 | `glRasterPos4s` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 85 | `glRasterPos4sv` | 2.12 Current raster position | 6 | `gl_raster.c` | `test_gl_raster.c` | none |
| 86 | `glRectd` | 2.9 Rectangles | 6 | `gl_prim.c` | `test_gl_prim.c` | none |
| 87 | `glRectdv` | 2.9 Rectangles | 6 | `gl_prim.c` | `test_gl_prim.c` | none |
| 88 | `glRectf` | 2.9 Rectangles | 6 | `gl_prim.c` | `test_gl_prim.c` | none |
| 89 | `glRectfv` | 2.9 Rectangles | 6 | `gl_prim.c` | `test_gl_prim.c` | none |
| 90 | `glRecti` | 2.9 Rectangles | 6 | `gl_prim.c` | `test_gl_prim.c` | none |
| 91 | `glRectiv` | 2.9 Rectangles | 6 | `gl_prim.c` | `test_gl_prim.c` | none |
| 92 | `glRects` | 2.9 Rectangles | 6 | `gl_prim.c` | `test_gl_prim.c` | none |
| 93 | `glRectsv` | 2.9 Rectangles | 6 | `gl_prim.c` | `test_gl_prim.c` | none |
| 94 | `glTexCoord1d` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 95 | `glTexCoord1dv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 96 | `glTexCoord1f` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 97 | `glTexCoord1fv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 98 | `glTexCoord1i` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 99 | `glTexCoord1iv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 100 | `glTexCoord1s` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 101 | `glTexCoord1sv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 102 | `glTexCoord2d` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 103 | `glTexCoord2dv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 104 | `glTexCoord2f` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 105 | `glTexCoord2fv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 106 | `glTexCoord2i` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 107 | `glTexCoord2iv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 108 | `glTexCoord2s` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 109 | `glTexCoord2sv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 110 | `glTexCoord3d` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 111 | `glTexCoord3dv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 112 | `glTexCoord3f` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 113 | `glTexCoord3fv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 114 | `glTexCoord3i` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 115 | `glTexCoord3iv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 116 | `glTexCoord3s` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 117 | `glTexCoord3sv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 118 | `glTexCoord4d` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 119 | `glTexCoord4dv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 120 | `glTexCoord4f` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 121 | `glTexCoord4fv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 122 | `glTexCoord4i` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 123 | `glTexCoord4iv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 124 | `glTexCoord4s` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 125 | `glTexCoord4sv` | 2.7 Vertex specification (texture coordinates) | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 126 | `glVertex2d` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 127 | `glVertex2dv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 128 | `glVertex2f` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 129 | `glVertex2fv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 130 | `glVertex2i` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 131 | `glVertex2iv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 132 | `glVertex2s` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 133 | `glVertex2sv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 134 | `glVertex3d` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 135 | `glVertex3dv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 136 | `glVertex3f` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 137 | `glVertex3fv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 138 | `glVertex3i` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 139 | `glVertex3iv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 140 | `glVertex3s` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 141 | `glVertex3sv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 142 | `glVertex4d` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 143 | `glVertex4dv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 144 | `glVertex4f` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 145 | `glVertex4fv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 146 | `glVertex4i` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 147 | `glVertex4iv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 148 | `glVertex4s` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 149 | `glVertex4sv` | 2.7 Vertex specification | 4 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 150 | `glClipPlane` | 2.11 Clipping | 4 | `gl_clip.c` | `test_gl_clip.c` | none |
| 151 | `glColorMaterial` | 2.13.3 ColorMaterial | 6 | `gl_light.c` | `test_gl_light.c` | none |
| 152 | `glCullFace` | 3.5.1 Basic polygon rasterization | 4 | `gl_prim.c` | `test_gl_prim.c` | none |
| 153 | `glFogf` | 3.9 Fog | 4 | `gl_fog.c` | `test_gl_fog.c` | none |
| 154 | `glFogfv` | 3.9 Fog | 4 | `gl_fog.c` | `test_gl_fog.c` | none |
| 155 | `glFogi` | 3.9 Fog | 4 | `gl_fog.c` | `test_gl_fog.c` | none |
| 156 | `glFogiv` | 3.9 Fog | 4 | `gl_fog.c` | `test_gl_fog.c` | none |
| 157 | `glFrontFace` | 2.13.1 Lighting (front and back faces) | 4 | `gl_prim.c` | `test_gl_prim.c` | none |
| 158 | `glHint` | 5.6 Hints | 4 | `gl_state.c` | `test_gl_state.c` | none |
| 159 | `glLightf` | 2.13.1 Lighting | 4, completed in 6 | `gl_light.c` | `test_gl_light.c` | none |
| 160 | `glLightfv` | 2.13.1 Lighting | 4, completed in 6 | `gl_light.c` | `test_gl_light.c` | none |
| 161 | `glLighti` | 2.13.1 Lighting | 4, completed in 6 | `gl_light.c` | `test_gl_light.c` | none |
| 162 | `glLightiv` | 2.13.1 Lighting | 4, completed in 6 | `gl_light.c` | `test_gl_light.c` | none |
| 163 | `glLightModelf` | 2.13.1 Lighting | 6 | `gl_light.c` | `test_gl_light.c` | none |
| 164 | `glLightModelfv` | 2.13.1 Lighting | 6 | `gl_light.c` | `test_gl_light.c` | none |
| 165 | `glLightModeli` | 2.13.1 Lighting | 6 | `gl_light.c` | `test_gl_light.c` | none |
| 166 | `glLightModeliv` | 2.13.1 Lighting | 6 | `gl_light.c` | `test_gl_light.c` | none |
| 167 | `glLineStipple` | 3.4 Line segments | 6 | `gl_state.c` | `test_gl_scene.c` | none |
| 168 | `glLineWidth` | 3.4 Line segments | 6 | `gl_state.c` | `test_gl_scene.c` | none |
| 169 | `glMaterialf` | 2.13.2 Lighting parameter specification | 4, completed in 6 | `gl_light.c` | `test_gl_light.c` | none |
| 170 | `glMaterialfv` | 2.13.2 Lighting parameter specification | 4, completed in 6 | `gl_light.c` | `test_gl_light.c` | none |
| 171 | `glMateriali` | 2.13.2 Lighting parameter specification | 4, completed in 6 | `gl_light.c` | `test_gl_light.c` | none |
| 172 | `glMaterialiv` | 2.13.2 Lighting parameter specification | 4, completed in 6 | `gl_light.c` | `test_gl_light.c` | none |
| 173 | `glPointSize` | 3.3 Points | 6 | `gl_state.c` | `test_gl_scene.c` | none |
| 174 | `glPolygonMode` | 3.5.4 Options controlling polygon rasterization | 4 | `gl_prim.c` | `test_gl_prim.c` | none |
| 175 | `glPolygonStipple` | 3.5.2 Stippling | 6 | `gl_state.c` | `test_gl_scene.c` | none |
| 176 | `glScissor` | 4.1.2 Scissor test | 4 | `gl_state.c` | `test_gl_scene.c` | none |
| 177 | `glShadeModel` | 2.13.7 Flatshading | 4 | `gl_prim.c` | `test_gl_prim.c` | none |
| 178 | `glTexParameterf` | 3.8.2 Texture parameters | 4 | `gl_texobj.c` | `test_gl_texobj.c` | none |
| 179 | `glTexParameterfv` | 3.8.2 Texture parameters | 4 | `gl_texobj.c` | `test_gl_texobj.c` | none |
| 180 | `glTexParameteri` | 3.8.2 Texture parameters | 4 | `gl_texobj.c` | `test_gl_texobj.c` | none |
| 181 | `glTexParameteriv` | 3.8.2 Texture parameters | 4 | `gl_texobj.c` | `test_gl_texobj.c` | none |
| 182 | `glTexImage1D` | 3.8 Texturing (TexImage1D) | 6 | `gl_teximage.c` | `test_gl_teximage.c` | none |
| 183 | `glTexImage2D` | 3.8 Texturing (TexImage2D) | 4 | `gl_teximage.c` | `test_gl_teximage.c` | none |
| 184 | `glTexEnvf` | 3.8.9 Texture environments and texture functions | 4 | `gl_texobj.c` | `test_gl_scene.c` | none |
| 185 | `glTexEnvfv` | 3.8.9 Texture environments and texture functions | 4 | `gl_texobj.c` | `test_gl_scene.c` | none |
| 186 | `glTexEnvi` | 3.8.9 Texture environments and texture functions | 4 | `gl_texobj.c` | `test_gl_scene.c` | none |
| 187 | `glTexEnviv` | 3.8.9 Texture environments and texture functions | 4 | `gl_texobj.c` | `test_gl_scene.c` | none |
| 188 | `glTexGend` | 2.10.4 Generating texture coordinates | 6 | `gl_texgen.c` | `test_gl_texgen.c` | none |
| 189 | `glTexGendv` | 2.10.4 Generating texture coordinates | 6 | `gl_texgen.c` | `test_gl_texgen.c` | none |
| 190 | `glTexGenf` | 2.10.4 Generating texture coordinates | 6 | `gl_texgen.c` | `test_gl_texgen.c` | none |
| 191 | `glTexGenfv` | 2.10.4 Generating texture coordinates | 6 | `gl_texgen.c` | `test_gl_texgen.c` | none |
| 192 | `glTexGeni` | 2.10.4 Generating texture coordinates | 6 | `gl_texgen.c` | `test_gl_texgen.c` | none |
| 193 | `glTexGeniv` | 2.10.4 Generating texture coordinates | 6 | `gl_texgen.c` | `test_gl_texgen.c` | none |
| 194 | `glFeedbackBuffer` | 5.3 Feedback | 6 | `gl_select.c` | `test_gl_select.c` | none |
| 195 | `glSelectBuffer` | 5.2 Selection | 6 | `gl_select.c` | `test_gl_select.c` | none |
| 196 | `glRenderMode` | 5.2 Selection | 6 | `gl_select.c` | `test_gl_select.c` | none |
| 197 | `glInitNames` | 5.2 Selection | 6 | `gl_select.c` | `test_gl_select.c` | none |
| 198 | `glLoadName` | 5.2 Selection | 6 | `gl_select.c` | `test_gl_select.c` | none |
| 199 | `glPassThrough` | 5.3 Feedback | 6 | `gl_select.c` | `test_gl_select.c` | none |
| 200 | `glPopName` | 5.2 Selection | 6 | `gl_select.c` | `test_gl_select.c` | none |
| 201 | `glPushName` | 5.2 Selection | 6 | `gl_select.c` | `test_gl_select.c` | none |
| 202 | `glDrawBuffer` | 4.2.1 Selecting a buffer for writing | 4 | `gl_surface.c` | `V9XGLP front/back scene` | none |
| 203 | `glClear` | 4.2.3 Clearing the buffers | 4 | `gl_state.c` | `test_gl_scene.c` | none |
| 204 | `glClearAccum` | 4.2.3 Clearing the buffers | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 205 | `glClearIndex` | 4.2.3 Clearing the buffers | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 206 | `glClearColor` | 4.2.3 Clearing the buffers | 4 | `gl_state.c` | `test_gl_scene.c` | none |
| 207 | `glClearStencil` | 4.2.3 Clearing the buffers | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 208 | `glClearDepth` | 4.2.3 Clearing the buffers | 4 | `gl_state.c` | `test_gl_scene.c` | none |
| 209 | `glStencilMask` | 4.2.2 Fine control of buffer updates | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 210 | `glColorMask` | 4.2.2 Fine control of buffer updates | 4 | `gl_state.c` | `test_gl_scene.c` | none |
| 211 | `glDepthMask` | 4.2.2 Fine control of buffer updates | 4 | `gl_state.c` | `test_gl_scene.c` | none |
| 212 | `glIndexMask` | 4.2.2 Fine control of buffer updates | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 213 | `glAccum` | 4.2.4 The accumulation buffer | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 214 | `glDisable` | 6.1.1 Simple queries / 2.11.1 Enable | 4 | `gl_state.c` | `test_gl_state.c` | none |
| 215 | `glEnable` | 6.1.1 Simple queries / 2.11.1 Enable | 4 | `gl_state.c` | `test_gl_state.c` | none |
| 216 | `glFinish` | 5.5 Flush and Finish | 4 | `gl_icd.c` | `V9XGLP ordering scene` | none |
| 217 | `glFlush` | 5.5 Flush and Finish | 4 | `gl_icd.c` | `V9XGLP ordering scene` | none |
| 218 | `glPopAttrib` | 6.1.11 Attribute stacks | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 219 | `glPushAttrib` | 6.1.11 Attribute stacks | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 220 | `glMap1d` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 221 | `glMap1f` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 222 | `glMap2d` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 223 | `glMap2f` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 224 | `glMapGrid1d` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 225 | `glMapGrid1f` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 226 | `glMapGrid2d` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 227 | `glMapGrid2f` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 228 | `glEvalCoord1d` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 229 | `glEvalCoord1dv` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 230 | `glEvalCoord1f` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 231 | `glEvalCoord1fv` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 232 | `glEvalCoord2d` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 233 | `glEvalCoord2dv` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 234 | `glEvalCoord2f` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 235 | `glEvalCoord2fv` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 236 | `glEvalMesh1` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 237 | `glEvalPoint1` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 238 | `glEvalMesh2` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 239 | `glEvalPoint2` | 5.1 Evaluators | 6 | `gl_eval.c` | `test_gl_eval.c` | none |
| 240 | `glAlphaFunc` | 4.1.3 Alpha test | 4 | `gl_state.c` | `test_gl_scene.c` | none |
| 241 | `glBlendFunc` | 4.1.6 Blending | 4 | `gl_state.c` | `test_gl_scene.c` | none |
| 242 | `glLogicOp` | 4.1.8 Logical operation | 6 | `gl_state.c` | `test_gl_scene.c` | none |
| 243 | `glStencilFunc` | 4.1.4 Stencil test | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 244 | `glStencilOp` | 4.1.4 Stencil test | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 245 | `glDepthFunc` | 4.1.5 Depth buffer test | 4 | `gl_state.c` | `test_gl_scene.c` | none |
| 246 | `glPixelZoom` | 3.6.4 Rasterization of pixel rectangles (zoom) | 6 | `gl_pixel.c` | `test_gl_pixel.c` | none |
| 247 | `glPixelTransferf` | 3.6.3 Pixel transfer modes | 6 | `gl_pixel.c` | `test_gl_pixel.c` | none |
| 248 | `glPixelTransferi` | 3.6.3 Pixel transfer modes | 6 | `gl_pixel.c` | `test_gl_pixel.c` | none |
| 249 | `glPixelStoref` | 3.6.1 Pixel storage modes | 4 | `gl_pixelstore.c` | `test_gl_pixelstore.c` | none |
| 250 | `glPixelStorei` | 3.6.1 Pixel storage modes | 4 | `gl_pixelstore.c` | `test_gl_pixelstore.c` | none |
| 251 | `glPixelMapfv` | 3.6.3 Pixel transfer modes (pixel maps) | 6 | `gl_pixel.c` | `test_gl_pixel.c` | none |
| 252 | `glPixelMapuiv` | 3.6.3 Pixel transfer modes (pixel maps) | 6 | `gl_pixel.c` | `test_gl_pixel.c` | none |
| 253 | `glPixelMapusv` | 3.6.3 Pixel transfer modes (pixel maps) | 6 | `gl_pixel.c` | `test_gl_pixel.c` | none |
| 254 | `glReadBuffer` | 4.3.2 Reading pixels (source buffer) | 4 | `gl_surface.c` | `V9XGLP front/back scene` | none |
| 255 | `glCopyPixels` | 4.3.3 Copying pixels | 6 | `gl_pixel.c` | `test_gl_pixel.c` | none |
| 256 | `glReadPixels` | 4.3.2 Reading pixels | 5, completed in 6 | `gl_pixels.c` | `test_gl_pixels.c` | partial: colour formats, unsigned byte, host-tested; GL_RGB on the software guest |
| 257 | `glDrawPixels` | 3.6.4 Rasterization of pixel rectangles | 6 | `gl_pixel.c` | `test_gl_pixel.c` | none |
| 258 | `glGetBooleanv` | 6.1.1 Simple queries | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 259 | `glGetClipPlane` | 6.1.3 Enumerated queries (clip planes) | 4 | `gl_get.c` | `test_gl_get.c` | none |
| 260 | `glGetDoublev` | 6.1.1 Simple queries | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 261 | `glGetError` | 2.5 GL errors | 4 | `gl_state.c` | `test_gl_state.c` | none |
| 262 | `glGetFloatv` | 6.1.1 Simple queries | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 263 | `glGetIntegerv` | 6.1.1 Simple queries | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 264 | `glGetLightfv` | 6.1.3 Enumerated queries (lighting) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 265 | `glGetLightiv` | 6.1.3 Enumerated queries (lighting) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 266 | `glGetMapdv` | 6.1.3 Enumerated queries (evaluators) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 267 | `glGetMapfv` | 6.1.3 Enumerated queries (evaluators) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 268 | `glGetMapiv` | 6.1.3 Enumerated queries (evaluators) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 269 | `glGetMaterialfv` | 6.1.3 Enumerated queries (lighting) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 270 | `glGetMaterialiv` | 6.1.3 Enumerated queries (lighting) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 271 | `glGetPixelMapfv` | 6.1.3 Enumerated queries (pixel maps) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 272 | `glGetPixelMapuiv` | 6.1.3 Enumerated queries (pixel maps) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 273 | `glGetPixelMapusv` | 6.1.3 Enumerated queries (pixel maps) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 274 | `glGetPolygonStipple` | 6.1.3 Enumerated queries (stipple) | 6 | `gl_get.c` | `test_gl_get.c` | none |
| 275 | `glGetString` | 6.1.5 String queries | 4 | `gl_get.c` | `test_gl_get.c` | none |
| 276 | `glGetTexEnvfv` | 6.1.3 Enumerated queries (texture) | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 277 | `glGetTexEnviv` | 6.1.3 Enumerated queries (texture) | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 278 | `glGetTexGendv` | 6.1.3 Enumerated queries (texture) | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 279 | `glGetTexGenfv` | 6.1.3 Enumerated queries (texture) | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 280 | `glGetTexGeniv` | 6.1.3 Enumerated queries (texture) | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 281 | `glGetTexImage` | 6.1.4 Texture queries | 6 | `gl_teximage.c` | `test_gl_teximage.c` | none |
| 282 | `glGetTexParameterfv` | 6.1.3 Enumerated queries (texture) | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 283 | `glGetTexParameteriv` | 6.1.3 Enumerated queries (texture) | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 284 | `glGetTexLevelParameterfv` | 6.1.3 Enumerated queries (texture) | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 285 | `glGetTexLevelParameteriv` | 6.1.3 Enumerated queries (texture) | 4, completed in 6 | `gl_get.c` | `test_gl_get.c` | none |
| 286 | `glIsEnabled` | 6.1.1 Simple queries / 2.11.1 Enable | 4 | `gl_state.c` | `test_gl_state.c` | none |
| 287 | `glIsList` | 5.4 Display lists | 6 | `gl_dlist.c` | `test_gl_dlist.c` | none |
| 288 | `glDepthRange` | 2.10.1 Controlling the viewport | 4 | `gl_xform.c` | `test_gl_xform.c` | none |
| 289 | `glFrustum` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 290 | `glLoadIdentity` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 291 | `glLoadMatrixf` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 292 | `glLoadMatrixd` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 293 | `glMatrixMode` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 294 | `glMultMatrixf` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 295 | `glMultMatrixd` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 296 | `glOrtho` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 297 | `glPopMatrix` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 298 | `glPushMatrix` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 299 | `glRotated` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 300 | `glRotatef` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 301 | `glScaled` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 302 | `glScalef` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 303 | `glTranslated` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 304 | `glTranslatef` | 2.10.2 Matrices | 4 | `gl_matrix.c` | `test_gl_matrix.c` | none |
| 305 | `glViewport` | 2.10.1 Controlling the viewport | 4 | `gl_xform.c` | `test_gl_xform.c` | none |
| 306 | `glArrayElement` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 307 | `glBindTexture` | 3.8.8 Texture objects | 4 | `gl_texobj.c` | `test_gl_texobj.c` | none |
| 308 | `glColorPointer` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 309 | `glDisableClientState` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 310 | `glDrawArrays` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 311 | `glDrawElements` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 312 | `glEdgeFlagPointer` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 313 | `glEnableClientState` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 314 | `glIndexPointer` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 315 | `glIndexub` | 2.7 Vertex specification (colour index) | 6 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 316 | `glIndexubv` | 2.7 Vertex specification (colour index) | 6 | `gl_vertex.c` | `test_gl_vertex.c` | none |
| 317 | `glInterleavedArrays` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 318 | `glNormalPointer` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 319 | `glPolygonOffset` | 3.5.5 Depth offset | 6 | `gl_xform.c` | `test_gl_scene.c` | none |
| 320 | `glTexCoordPointer` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 321 | `glVertexPointer` | 2.8 Vertex arrays | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 322 | `glAreTexturesResident` | 3.8.8 Texture objects (residency and priority) | 6 | `gl_texobj.c` | `test_gl_texobj.c` | none |
| 323 | `glCopyTexImage1D` | 3.8.1 Alternate image specification | 6 | `gl_teximage.c` | `test_gl_teximage.c` | none |
| 324 | `glCopyTexImage2D` | 3.8.1 Alternate image specification | 6 | `gl_teximage.c` | `test_gl_teximage.c` | none |
| 325 | `glCopyTexSubImage1D` | 3.8.1 Alternate image specification | 6 | `gl_teximage.c` | `test_gl_teximage.c` | none |
| 326 | `glCopyTexSubImage2D` | 3.8.1 Alternate image specification | 6 | `gl_teximage.c` | `test_gl_teximage.c` | none |
| 327 | `glDeleteTextures` | 3.8.8 Texture objects | 4 | `gl_texobj.c` | `test_gl_texobj.c` | none |
| 328 | `glGenTextures` | 3.8.8 Texture objects | 4 | `gl_texobj.c` | `test_gl_texobj.c` | none |
| 329 | `glGetPointerv` | 6.1.3 Enumerated queries (vertex array pointers) | 4 | `gl_varray.c` | `test_gl_varray.c` | none |
| 330 | `glIsTexture` | 3.8.8 Texture objects | 4 | `gl_texobj.c` | `test_gl_texobj.c` | none |
| 331 | `glPrioritizeTextures` | 3.8.8 Texture objects (residency and priority) | 6 | `gl_texobj.c` | `test_gl_texobj.c` | none |
| 332 | `glTexSubImage1D` | 3.8.1 Alternate image specification | 6 | `gl_teximage.c` | `test_gl_teximage.c` | none |
| 333 | `glTexSubImage2D` | 3.8.1 Alternate image specification (TexSubImage2D) | 4 | `gl_teximage.c` | `test_gl_teximage.c` | none |
| 334 | `glPopClientAttrib` | 6.1.11 Attribute stacks | 6 | `gl_state.c` | `test_gl_state.c` | none |
| 335 | `glPushClientAttrib` | 6.1.11 Attribute stacks | 6 | `gl_state.c` | `test_gl_state.c` | none |

