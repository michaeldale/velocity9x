# Engine-placed render targets are never freed

Date: 2026-11-15

Status: open. Found by reading, not measured.

`v9x_d3d_place_release` (`src/display32/d3d/d3d_place.c`) frees a block
only for a surface whose caps include `MIPMAP`, `ZBUFFER` or `TEXTURE`.

Several engines' `create_surface` also place an offscreen render target
(`OFFSCREENPLAIN | 3DDEVICE`, not primary, not flip) with
`v9x_d3d_place_each`, because DirectDraw's heap gives it a pitch the
engine cannot draw to:
- `d3d_eg.c`, `d3d_r6.c`, `d3d_vi.c`, `d3d_nv50.c`, `d3d_r128.c` and
  `d3d_mga.c`, by the check they share;
- `d3d_rv280.c` from 2026-11-15.

Such a surface has `lpVidMemHeap` null, so DirectDraw does not free it
either. Each one leaks its block. The ICD makes one OpenGL back buffer
per drawable size, so a resized or recreated window leaks a back buffer
each time. That is the visible case.

A fix would add `3DDEVICE` to the caps `v9x_d3d_place_release` accepts.
The signature checks (block start in `dwReserved1`, no heap) already keep
a surface DirectDraw placed out of it. That fix is shared code for seven
engines, and none of them has been measured with it.
