# RIVA 128ZX Direct3D

Phase 7 of `docs/plans/nvidia-riva128.md`. Machine: M-7V636C (remote help,
owner at the monitor only by arrangement).

## What the hardware is

One object, type 17h (envytools' D3D, NVIDIA's "Direct3D 5 textured
triangle with zeta buffer"): screen-space vertices as IEEE floats (x, y, z,
rhw, u, v, a colour and a specular/fog word), perspective-correct
texturing, a 16-bit zeta buffer, alpha test, a fixed blend (source factor
from source alpha or destination colour, or ADD), fog, colour key, wrap,
mirror and clamp. Textures are A1R5G5B5, X1R5G5B5, A4R4G4B4 or R5G6B5,
power-of-two up to 2048, read through a DMA object. It draws into
PGRAPH surface 2 with zeta in surface 3, at X1R5G5B5 only. That is the
ViRGE and Rage II class of Direct3D: DX5 TLVERTEX triangles, one texture,
no multitexture, no stencil.

The tree's HALs of that class are `src/display32/d3d/d3d_virge.c`,
`d3d_rage2.c` and `d3d_sis6326.c`; `d3d_select.c` picks one by engine type.
This one is `d3d_nv3.c` with a pure `nv3_3d.c` builder, host-tested, as
`nv3_2d.c` is for the 2D engine.

## Sources

The `build\external\nv3\` set of the main plan: envytools' `nv3_3d.xml`,
`nv3_pgraph.xml`, `hwtest/pgraph_class_d3d0.cc` (the methods' valid values),
`nvhw/pgraph_d3d_nv3.c` (the float conversions and the blend), and, read
from envytools on 2026-11-03, `nvhw/pgraph.c` `pgraph_prep_draw` (when a
triangle draws). rivafb's tables for the objects. 86Box's `vid_nv3.h` for
the DMA object's flag bits; 86Box draws no 3D.

## Steps

1. **First triangle (done 2026-11-03).** `V9XN32D /d3d` drew a textured,
   fogged triangle off screen
   ([record](../decisions/2026-11-03-riva128-first-triangle.md)): the
   trigger, modulation, the fog byte's sense, video memory texturing,
   swizzled textures.
2. **The rest of the state, by probe.** Z (LESS and LEQUAL, writes on, the
   value written for a given z), the edge rule against the probe's 36 extra
   pixels, perspective with two rhw values, a texture that tells every
   texel apart (to prove the swizzle and the texel centre), the blend
   factors, alpha test, colour key, the bilinear filter's SPREAD and
   SIZE_ADJUST fields, and the 8 MiB DMA object placed clear of RAMRO and
   RAMFC (RAMIN 4000h has 16 KiB free).
3. **5:5:5 desktops (decision B, already taken).** The D3D object cannot
   render 5:6:5, so 16 bpp modes run 5:5:5: the VBE mode, then PRAMDAC
   general control's colour bit, then a 5:5:5 mode to Windows. The driver
   already reports 5:5:5 (`is555`). Needs the owner at the monitor once.
4. **The builder.** `nv3_3d.c`: render-state words from Direct3D state,
   the vertex words (the fog byte inverted), triangle batches as slot
   triples, swizzled texture uploads. Host tests from steps 1-2's numbers.
5. **The HAL.** `d3d_nv3.c` on the core: context create and destroy,
   render target and zeta at surfaces 2 and 3, texture create, load and
   swizzle into the DirectDraw heap, DrawPrimitive and DrawPrimitive2 over
   TLVERTEX, the D3D caps the hardware has and no more. Shares the engine
   with eng_nv3.c's 2D and GDI through the same bring-up, which gains the
   D3D object, the DMA object and subchannel 7.
6. **Games.** The DX5-era set already used on other machines, on M-7V636C
   at 640x480x16 (5:5:5).

## Unknowns

- How a Direct3D render target that is not the screen's pitch is handled:
  the surface registers take any pitch up to 1FF0h, so probably freely.
- Whether the zeta surface has to share the colour surface's pitch.
- What clears zeta: the 2D engine's fill into surface 3 is the likely way.
- Whether the texture cache needs flushing after a texture changes in
  place.
