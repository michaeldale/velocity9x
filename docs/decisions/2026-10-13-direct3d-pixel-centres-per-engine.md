# Direct3D's pixel centres move onto each engine's: half a pixel on the GMA 950/900, the Voodoo4 and software

Date: 2026-10-13. Machines:
- MICHAEL-NETBOOK, GMA 950, boots 177 (0.17.0 at `bf92ca21`) and 178
  (`9f17b089`);
- A8U4I5, Voodoo4 4500 PCI, boots 445 and 446;
- the Hellbender 86Box guest, S3 ViRGE, boots 664-666.

Evidence:
[direct3d-pixel-centres-2026-10-13](../probe/direct3d-pixel-centres-2026-10-13/).
Follows
[2026-10-13-voodoo4-opengl-and-subpixel-correction.md](2026-10-13-voodoo4-opengl-and-subpixel-correction.md).

## The problem

- Direct3D puts a pixel's centre on its whole coordinate.
- The render core's contract samples at x+0.5, OpenGL's rule
  ([cpu-rasterizer-contract.md](../specifications/cpu-rasterizer-contract.md),
  Coverage). So do the CPU rasterizer and some engines.
- Direct3D vertices reached the engines unmoved.

V9XDDP's TexPhase draws a 16x16 texture 1:1, point-sampled, as a quad at
whole-pixel offsets and at half-, quarter- and eighth-pixel offsets, and
reads which texel each pixel took. On the GMA 950 at 0.17.0's first build
(netbook boot 177):

- a quad moved half a pixel up and left read the next texel along
  (`123456789ABCDEF.`). Direct3D's rule gives each texel its own pixel;
- V9XGLP's twin read OpenGL's rules exactly;
- the Voodoo4 had shown the same before its engine shifted its own
  vertices (boots 438-441). Its Half-Life console font lost strokes.

## A global move, and why it was wrong

The first cut moved every Direct3D vertex by half a pixel for every engine.

- **It was too big for the ATI floppy.** A 6 KB copy buffer put it
  2,188 bytes over 1.44 MB.
- **It broke the ViRGE.** The Hellbender guest's ViRGE had read Direct3D's
  coverage before: first pixel covered at offset 0, uncovered at +0.5.
  With the move its first pixel went uncovered at offset 0, and at the
  sub-pixel offsets coverage moved by half a pixel. The ViRGE samples at
  Direct3D's centres, as a chip designed for Direct3D would.

## What was built

- **Each engine says which centre it samples**
  (`V9X_D3D_ENGINE_LIMITS.pixel_centre_half`, appended).
  - 1 for Gen3, the VSA-100 and the software engine.
  - 0, unchanged, for the ViRGE (measured) and for the Millennium II,
    SiS 6326, Mach64 and Rage IIC (not measured).
- **The list builder makes the move** (`V9X_R3D_LIST.pixel_offset`). Each
  triangle is copied with the offset before the on-target test and the
  clip, into the staging the builder already had.
  - The application's array is never written.
  - A record resent after a refusal is not moved twice.
  - A vertex on the far edge is clipped back to it.
  - No buffer is added; the HAL is the same size.
  - Culling sees the caller's triangle, which a translation does not
    change.
- **RenderPrimitive** moves its local copy.
- **The Voodoo4's own Direct3D shift is removed.**
- **The software engine** clamps Direct3D vertices at the far edge rather
  than a column short, as it already did for explicit draws.
- **The render interface** (OpenGL, Glide) passes `pixel_offset` 0, so its
  vertices are unchanged.
- **TexPhase** now uses a 1555 texture, which the ViRGE samples, and reads
  pixels through the target's own layout; the Hellbender desktop is 555.

## Measured

| Engine | TexPhase (Direct3D) | V9XDDP pass/fail | V9XGLP |
|---|---|---|---|
| GMA 950, boot 178 | Direct3D's rules at all eleven offsets, rows and columns | 203/19, same keys as boot 177 | 53 lines identical |
| Voodoo4, boots 445/446 | Identical to boot 444 (exact) | Identical to boot 444 | Identical |
| ViRGE, per-engine build | Identical to its previous HAL, all 22 lines | 196/26, same keys | not run |

On the GMA 950, the raw values that moved are half-pixel effects: the
Gouraud ramp samples at x36, PerspRow46 and TunL_2_I.

## Not established

- **The Millennium II, SiS 6326, Mach64 and Rage IIC.** Their behaviour is
  0.16.0's, right or wrong, until TexPhase runs on each.
- **The ViRGE on real hardware.** 86Box's ViRGE texturing is too coarse at
  1:1 to read texels exactly, so only coverage was read.
- **The GMA 900 (915GM).** It shares the Gen3 engine and its setting and is
  expected to behave as the 950 does, but it has not been measured.
- **OpenGL on the ViRGE.** If it samples at Direct3D's centres, OpenGL
  there is half a pixel off. That path is not touched here.
- **Half-Life's Direct3D console on the netbook** was not looked at after
  the change.
