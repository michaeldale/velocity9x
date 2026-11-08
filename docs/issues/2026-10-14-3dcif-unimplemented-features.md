# 3DCIF: mipmaps, tiled textures, meshes and TLVERTEX are not drawn

Date: 2026-10-14

Status: Open, parked by Michael on 2026-10-14.

ATI3DCIF.DLL (`src/cif/cif_dll.c`) does not yet handle:

- **Mipmaps.** Only level 0 is converted, and it is drawn without level
  selection. A texture larger than the engine takes keeps the first level
  that fits (`v9x_cif_texfmt_fit_level`). `v9x_cif_texfmt_levels` counts
  the chain, but nothing uploads it.
- **Tiled textures** (C3D_TMAP `eTexTiling` ON, the Rage Pro layout) are
  registered and drawn untextured. Only the Knight demos tile, and only
  because `WIN.INI [AtiDemo] TexAddrTiling` defaults to TRUE; A8U4I5 runs
  with it FALSE. A title that tiles would need the layout decoded.
- **RenderPrimMesh** and **RenderIndexedPrim** draw nothing.
- **C3D_EV_TLVERTEX** vertices are not converted. No SDK demo uses them.
- **Lines, points and rectangles** are not drawn.

No demo in the census needs these beyond what is noted, so each waits for
a title that does.

## Status 2026-11-08 (audit)

Lines have been drawn since 5b44485b (2026-10-16), each as two triangles. Assault Rigs on A8U4I5's Rage XL (boot 482) went from 50,097 skipped line draws to none over 2,877 frames. Points, rectangles, RenderPrimMesh, RenderIndexedPrim, TLVERTEX, mipmaps and tiled textures are still not drawn. The comment at `src/cif/cif_dll.c` still lists lines as not drawn.
