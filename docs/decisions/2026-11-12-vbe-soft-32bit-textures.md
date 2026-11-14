# The software Direct3D engine samples 32-bit textures: V9XD32 T22 white to exact

Date: 2026-11-12

Status: **measured, one machine.** M-JJN1ZZ's Radeon R3E (Mullins), remote
help, on the vbe set by WININIT over its ati set, 1024x768x16, then back
to ati. Build 27d432c3 plus this change (dirty).

Evidence: [`../probe/vbe-soft-8888-2026-11-12/`](../probe/vbe-soft-8888-2026-11-12/)
(`vbe/`: the probes on the vbe set; `ati-restored/`: on the ati set after).
The failing run is in [`../probe/shadow-ati-vbe-2026-11-12/vbe/`](../probe/shadow-ati-vbe-2026-11-12/vbe/).

## What was wrong

V9XD32's T22 (added 2026-11-11 for Gen3's 32-bit textures) drew white on
the vbe package: create, lock, handle and draw all returned 0, and every
pixel was `0x00FFFFFF`
([record](2026-11-12-ati-screen-shadow.md#the-other-machines-afternoon-committed-build-7da1bcb1)).

Not a sampling fault. The software engine (`d3d_soft.c`) classified 16-bit
formats only - ARGB1555, ARGB4444, RGB565 - and refused anything else,
counting it in `texture_refused_format`; a refused texture draws as
untextured vertex colour, here white. The rasterizer (`d3d_raster.c`) read
every texel as a 16-bit word, so it could not have sampled them either.
The engine published only the three 16-bit formats, so a program that
enumerates formats would not have picked 32-bit ones; V9XD32 and the
Direct3D 8 runtime create them by name.

## The change

- `d3d_raster.c`: `V9X_D3D_RASTER_TEXFMT_ARGB8888` and `_XRGB8888`. The
  texel fetch reads a DWORD for them and a WORD otherwise
  (`V9X_D3D_RASTER_TEXEL`, one test a fetch), the channel fields are eight
  bits wide (the existing replication formula leaves a byte as it is),
  ARGB8888's alpha is the top byte, and the pitch checks for the texture
  and every mip level count four bytes a texel.
- `d3d_soft.c`: classifies 32-bit RGB with 00FF0000/0000FF00/000000FF
  masks as ARGB8888 when the surface has an FF000000 alpha mask and as
  XRGB8888 otherwise, and publishes both, filling the shared block's five
  texture-format slots.

The S3D unit the software engine is meant to stay interchangeable with
carries ARGB8888 as well, so the two engines still agree on it.

Host tests written first and seen failing (11 checks): both formats decode
to the expected 5:6:5 pixel, ARGB8888's eight-bit alpha shows through
DECALALPHA over a black vertex (about half red) where XRGB8888 is opaque
(full red), and a pitch of two bytes a texel is refused for either.

## Measured

On the vbe set (boot 41): V9XD32 `Failures=0`, `Result=PASS`, T22's four
quadrants exact for both formats (`T22AOk` and `T22XOk` `0x0000000F`;
pixels `002040C0`, `00C08020`, `0010E070`, `0090A0F0`, against all
`00FFFFFF` before). V9XGLP, V9XGLIDP (0 failures) and V9XR3DP pass. Back on
the ati set (boot 42) all five probe runs pass.

Gates: `build-host.ps1`, `run-checks.ps1`.

## Not covered

- A game that picks 32-bit textures on the software engine; the cost of
  the per-texel width test, not measured (the 16-bit path now tests a
  flag it did not before).
- 32 bpp desktops on vbe with this change.
