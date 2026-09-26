# Phase 5: OpenGL textures on the ViRGE - what draws and what cannot

Date: 2026-09-26. Machine: A8U4I5 (physical ViRGE/DX, Win98 SE,
1024x768 at 16 bpp, 555 - the only 16-bit layout the S3D writes).
Driver set from the s3 package at this change (boot 120; sizes and HAL
hash checked), ICD registered under OpenGLDrivers.

## Before

Michael ran the Quake 3 demo through the ICD. Its exit counters: 674,148
batches, about 32 million triangles, every one refused (result 4) and
none drawn; the screen showed the desktop colour. The ViRGE offered the
interface no texture path at all: CPU levels it cannot read, and no
software fallback, because the ViRGE and the CPU sharing a 555 frame has
not been measured.

## What changed

- Describe publishes the ViRGE's sampler: square, a power of two, up to
  512 texels, ARGB1555 or ARGB4444 - the S3D samples no RGB565, so its
  texture formats now say so.
- The ViRGE's accepts refuses, for interface draws, a surface texture its
  bind would draw untextured (`v9x_d3d_virge_texture_bindable`: caps,
  shape, tight pitch, format), as Gen3's does.
- The ICD stores an RGB image as ARGB1555 with alpha one for an engine
  without 565, converting at upload (`v9x_gl_tex_565_to_1555`), and picks
  the alpha op that keeps the result: the texel's when nothing reads the
  fragment's alpha, their product (one times the fragment's) when
  something does (`v9x_gl_tex_as_1555`). Host tests first.
- When nothing reads the result's alpha, REPLACE and MODULATE on a
  texture with alpha now take the texel's alpha, so an engine with
  MODULATE but no MODULATEALPHA - the ViRGE - draws them. Host test
  first.

## Measured

V9XGLP (`2026-09-26-phase5-virge-hardware-textures-V9XGLP.ini`):

- Drawn correctly: single-level textures (the 256x256 sky scenes, the
  texture rewritten between draws with A, B, C in order, the far quads,
  the gridded ceiling), converted RGB textures, orange MODULATE, a 4x4
  texture, and the scissor's inside and one-pixel-outside edges -
  within 555 rounding.
- Refused, as the ViRGE's rules require, and not drawn: non-square
  textures, textures under 4 texels, blended textured draws whose alpha
  is not the texel's (translucent MODULATE with vertex alpha) and the
  ZERO/SRC_COLOR lightmap multiply.
- Degraded: the mip scene reads level 0 at every size - DirectDraw placed
  the chain's levels apart, and the ViRGE draws a non-contiguous chain
  from its top level (its existing, counted behaviour).
- Wrong, not yet explained: the near-plane half of the perspective
  texture quad does not draw; the scissored clear does not land; the far
  corner pixel of the scissor box is black where the software engine and
  Gen3 draw it red; one screen pixel differs from the back buffer after
  a swap.

Quake 3 demo, menu (`...-quake3-counters.txt`, `...-quake3-menu.png`):
the textured menu model draws; 158,658 triangles drawn by the ViRGE,
1,743,732 refused for non-square textures, 1,161,552 refused for state
the ViRGE cannot express - RGBA textures under MODULATE blended with
source alpha (the ViRGE blends with the texel's alpha alone) and
additive ONE/ONE. The menu's text and pictures are among those. No
timedemo: the demo pak has no `four` demo.

## Standing

Hardware textures work on the ViRGE within what its S3D can express
exactly. What Quake 3 needs beyond that - non-square maps, blends other
than source alpha over its inverse with texel alpha, the alpha test -
has no ViRGE expression. The two ways forward are padding non-square
CLAMP textures to a square (scaling their coordinates), and the software
fallback on the ViRGE after measuring the mixed 555 frame.
