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
2. **The rest of the state, by probe (done 2026-11-03).** `V9XN32D
   /d3dstate`, 14 cases
   ([record](../decisions/2026-11-03-riva128-d3d-state.md)):
   - Every texel swizzled.
   - Point sampling on interpolator 0, bilinear on interpolator 2 (half a
     texel off Direct3D).
   - Perspective done by the engine.
   - Zeta = z · 65536, comparisons in Direct3D's sense.
   - Alpha test, source alpha blend and colour key work.
   - The edge rule is the top-left rule at integer points with the vertex
     fraction dropped.
   - The whole-memory DMA object at RAMIN 4000h works.

   Left for step 5, against games:
   - LEQUAL and the other comparisons.
   - Z_PERSPECTIVE.
   - The DSTCOLOR and ZERO blends.
   - FILTER's fields.
   - Mipmaps.
   - Wrap modes.
   - The dither switch.
3. **5:5:5 desktops (done 2026-11-03, boot 26,
   [record](../decisions/2026-11-03-riva128-555-desktop.md)).** The VBE
   5:6:5 mode, then PRAMDAC general control bit 12 cleared by the family's
   `place_desktop` hook, then 5:5:5 to Windows. V9XDDP went from 35 to 200
   of 227 checks; V9XGDI passes at 16, 32 and 8 bpp. Still owed: the owner
   looks at the monitor once.
4. **The builder (done 2026-11-03).** `src/chipsets/nvidia/nv3_3d.c`,
   `include/velocity9x/nv3_3d.h`, `tests/host/test_nv3_3d.c`. It builds:
   - the object's RAMIN writes, the whole-memory DMA object and its
     page entries;
   - the target (surfaces 2 and 3 and the clip);
   - the six state methods from Direct3D state;
   - vertex methods with slot triples and the fog byte inverted;
   - the swizzle.

   The expected values are the probes' own CONFIG and specular words. Some
   choices come from the chip's blend. It has no alpha-free blend, so an
   opaque draw forces vertex alpha FFh and reads A1R5G5B5 texels as
   X1R5G5B5, and opaque A4R4G4B4 is refused. Of Direct3D's blend pairs only
   ONE/ZERO, SRCALPHA/INVSRCALPHA, ONE/ONE and DESTCOLOR/ZERO map. The last
   is envytools' model, not drawn. Textures are square: TEXTURE_FORMAT has
   one size for u and v, so the HAL pads non-square textures. Culling is
   left to the HAL (CONFIG culling NONE). The texel-centre correction for
   bilinear is not in the builder; the HAL applies it to the float
   coordinates (2026-11-04).
5. **The HAL (first light 2026-11-03,
   [record](../decisions/2026-11-03-riva128-d3d-hal-first-light.md)).**
   V9XDDP's triangles and textured checks draw, in X1R5G5B5 on a 5:6:5
   desktop. Z works, all eight comparisons and the write mask (V9XDDP's
   step log, not its INI, which dropped keys). Opaque A4R4G4B4 draws from
   an alpha-free copy (boot 25). Open:
   - Opaque A4R4G4B4 under the alpha test is refused. **Done 2026-11-07,
     boot 44 ([record](../decisions/2026-11-07-riva128-a4r4g4b4-alpha-test.md)):**
     the opaque copy carries the test's verdict as A1R5G5B5's alpha.
   - DECALALPHA is refused.
   - After step 3, V9XDDP's 27 failing checks: mipmap level choice, colour
     key, specular, Z with perspective, mixed 2D/3D, 32-bit textures,
     bilinear's half texel, FlipPixelOk. **Mipmaps done 2026-11-04, boot
     30 ([record](../decisions/2026-11-04-riva128-mipmaps.md)):** chains
     to 8x8, bias F4h, 209 of 227; the two trilinear blend checks stay
     failed (no blend between levels). **Colour key done, boot 31
     ([record](../decisions/2026-11-04-riva128-colour-key.md)):** the chip
     compares the alpha bit, so the key takes the texels' own; 211 of 227.
     **Specular done, boot 32
     ([record](../decisions/2026-11-04-riva128-specular.md)):** folded
     into untextured colours, a second add pass after textured triangles;
     213 of 227. **"Z with perspective" was never a failure
     ([record](../decisions/2026-11-04-riva128-zprivate.md)):** the D3DZP
     keys run only under V9XDDP `/zprivate`, where both pass (boot 32).
     **Nor mixed 2D/3D ([record](../decisions/2026-11-04-riva128-mixed.md)):**
     the four Mixed keys run only under `/mixed`, where all pass, 217 of
     227 in that run. **The two 8888 checks stay failed
     ([record](../decisions/2026-11-04-riva128-no-8888-textures.md)):** no
     32-bit texture format, as on Gen3. **DECALALPHA done, boot 33
     ([record](../decisions/2026-11-04-riva128-decal-alpha.md)):** DECAL
     where the texture has no partial alpha, else two passes; 215 of 227.
     **Bilinear's half texel done, boot 34
     ([record](../decisions/2026-11-04-riva128-bilinear-centre.md)):** 216
     of 227. Left: the trilinear blend pair, the 8888 pair, FlipPixelOk.

   The original scope: `d3d_nv3.c` on the core: context create and destroy,
   render target and zeta at surfaces 2 and 3, texture create, load and
   swizzle into the DirectDraw heap, DrawPrimitive and DrawPrimitive2 over
   TLVERTEX, the D3D caps the hardware has and no more. Shares the engine
   with eng_nv3.c's 2D and GDI through the same bring-up, which gains the
   D3D object, the DMA object and subchannel 7.
6. **Games.** The DX5-era set already used on other machines, on M-7V636C
   at 640x480x16 (5:5:5). **Half-Life (2026-11-04, boot 27,
   [record](../decisions/2026-11-04-riva128-half-life-d3d.md))** draws
   `c1a0` windowed on the HAL. SRCALPHA/ONE, a fifth of its batches, now
   goes to the chip's add with the vertex colour premultiplied (texels of
   alpha 1, no fog); 0.3% of batches were still refused (opaque A4R4G4B4
   under the alpha test), none since boot 44. **Unreal Tournament (2026-11-04, boot 35,
   [record](../decisions/2026-11-04-riva128-unreal-tournament.md))**
   draws its intro windowed; its ONE/INVSRCCOLOR translucency is drawn as
   the add, by Michael's choice, and 137 of 32,740 batches are refused
   (ONE/SRCALPHA). **Thief (2026-11-04, boot 36,
   [record](../decisions/2026-11-04-riva128-thief.md))** draws Lord
   Bafford's Manor fullscreen, seen through the flip-path frame grab: 719,438
   draws, none refused. **3DMark 99 Max (2026-11-07, boots 42-43,
   [record](../decisions/2026-11-07-riva128-3dmark99-unplaced-textures.md))**
   at 800x600x16 refused 11,837 of 32,260 draws on textures DirectDraw had
   placed when the NV3 had no room. A full heap now fails the create as
   out of video memory: 40,951 draws, none refused. Left: the owner looks
   at a fullscreen game, and the guest's other D3D games (Thief 2, Final
   Fantasy VII, Homeworld).

## Unknowns

- How a Direct3D render target that is not the screen's pitch is handled:
  the surface registers take any pitch up to 1FF0h, so probably freely.
- Whether the zeta surface has to share the colour surface's pitch.
- What clears zeta: the 2D engine's fill into surface 3 is the likely way.
  The value is FFFFh (step 2); the probe cleared it with the CPU.
- Whether the texture cache needs flushing after a texture changes in
  place.
