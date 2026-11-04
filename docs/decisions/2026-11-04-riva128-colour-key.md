# RIVA 128ZX: the colour key compares the alpha bit too

Date: 2026-11-04

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition, boots 30-31, desktop 800x600x16 at 5:5:5. Plan:
`docs/plans/nvidia-riva128-d3d.md`, step 5's open list. Files:
`docs/probe/m7v636c-riva128-games-2026-11-04/b30-V9XDDT.TXT` and
`b31-V9XDDT.TXT`.

## What failed

V9XDDP's two colour key checks, on boot 30:
- ColorKey: an A1R5G5B5 texture filled FC1Fh (magenta, alpha bit set),
  key 7C1Fh. The pixel read 7C1Fh: magenta drawn, the key ignored.
- ColorKeyHalves: R5G6B5 passed (left kept the blue target, right green).
  A1R5G5B5, texels FC1Fh and 83E0h, key 7C1Fh: the left read 7C1Fh.

The HAL sent the DirectDraw key as given. Direct3D compares the colour
alone; the chip compares all 16 bits of the texel it reads, alpha
included. The state probe's ColourKey case (2026-11-03) keyed with 9080h
on texels whose alpha bit was set, so it never saw the difference. This
is inferred from the two checks, not probed on its own.

## What was built

`d3d_nv3.c` turns the key into the texel the chip compares
(`v9x_d3d_nv3_key`):
- R5G6B5: as given.
- A1R5G5B5 and X1R5G5B5: the colour with the top bit every texel of
  level 0 shares (the alpha scan, now telling all-one, all-zero and mixed
  apart). With a mix, only under the source alpha blend, where alpha-0
  texels draw nothing anyway.
- A4R4G4B4 sampled through the opaque copy: the key converted as its
  texels were. Blended: alpha F, only when every alpha is F.
- Anything else is refused (policy slot 17) rather than keyed wrongly.

## Boot 31

ColorKeyRaw 0000h (nothing drawn), ColorKeyHalves 1555 left 001Fh
(target blue kept) and right 03E0h; ColorKeyOk and ColorKeyHalvesOk 1.
211 of 227 `Ok` keys, none of boot 30's lost. 592 batches drawn, 7
refused, none for the key, no engine timeout.

## Not done

- DirectDraw's key range (low to high) is keyed at its low value only, as
  before.
- A keyed texture with mixed alpha drawn opaque or added is refused; no
  application has been seen to need it.
- Lower mip levels are not scanned: the key's top bit comes from level 0.
