# RIVA 128ZX: specular without a specular input

Date: 2026-11-04

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition, boots 31-32, desktop 800x600x16 at 5:5:5. Plan:
`docs/plans/nvidia-riva128-d3d.md`, step 5's open list. Files:
`docs/probe/m7v636c-riva128-games-2026-11-04/b32-*`.

## Why

The D3D object's vertex has no specular colour: the word in the
TLVERTEX's specular place carries the slot triples and the fog byte
(nv3_3d.h). The HAL dropped the specular. V9XDDP's two checks failed:
- D3DSpecularGouraudOk: untextured, colour black, specular green.
- D3DSpecularTexOk: an 8x8 green texel, MODULATE with white, red
  specular; Direct3D's add after the texture gives yellow. Boot 31 read
  03E0h, green.

The core folds specular into the colours for the software engine only.

## What was built

- **Untextured:** the specular is added to each vertex colour,
  saturating (`v9x_nv3_3d_add_specular`, host-tested). The chip
  interpolates linearly, so the sum interpolates as the two would apart;
  it differs only where a vertex's sum saturates.
- **Textured:** a second pass after each triangle. The white texture, the
  specular as the vertex colour, the chip's saturating add. Z compares as
  the first pass did, or EQUAL where the first pass wrote zeta, and no
  zeta is written. Per triangle, so a later triangle covers an earlier
  one's specular as it covers the rest of it.
  - It is Direct3D's sum only when the first pass replaced the pixel or
    added to it, drew every pixel of the triangle and was not fogged. So
    it applies with ONE/ZERO or ONE/ONE, no alpha test, no colour key, no
    fog, and some vertex with non-black specular. Elsewhere the specular
    is still left out: refusing would lose the whole draw.
- The caps still do not claim specular.

## Boot 32

D3DSpecularGouraudOk 1. D3DSpecularTexOk 1, 7FE0h (yellow). 213 of 227
`Ok` keys, none of boot 31's lost. 592 batches drawn, 7 refused, no
engine timeout.

Half-Life, windowed as before: the same door view, 303,728 batches drawn,
925 refused, no timeout (`hl-b32-SNAP.INI`), against boot 30's 304,309
and 924. Whether any of its batches took the second pass is not known.

## Not done

- Specular under a blend, the alpha test, a colour key or fog is still
  dropped.
- No counter records how many batches took the second pass; V9XTRACE has
  no spare slot that fits.
- An EQUAL second pass also passes where the first pass failed its test
  but zeta already held the same depth, adding specular there.
