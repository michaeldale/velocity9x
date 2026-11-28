# Rage XL: V9XD32's additive blend draws nothing and its 32-bit textures draw black

Date: 2026-11-26. Machine: A8U4I5, Rage XL PCI (`1002:4752`), the 0.61.0
tree's `ati` set, 800x600x16. Status: open, cause not looked at. Left
for a later release with the other Rage XL improvements (Michael,
2026-11-26); a README known issue in 1.0. First
runs of these two tests on a Mach64; no earlier result to compare.

## What is seen

V9XD32 `/bpp16` fails two tests
([report](../probe/ragexl-1.0-2026-11-26/ragexl/v9xd32-bpp16-V9XD32.TXT)):

- **T20:** `T20Ok=FFF3`. Every cell passes but the two additive-blend
  ones: `T20AddRed` and `T20AddBlue` read 0. `T20AddHr` is 0, so the
  draw was accepted.
- **T22:** the A8R8G8B8 and X8R8G8B8 textures are created, locked, given
  handles and drawn with every `Hr` 0, but all eight judged pixels read
  0 (`T22AOk=0`, `T22XOk=0`).

3DMark 2000 runs to its score on the same boot, its grabbed frames
whole, with 155 of 479,328 draws refused (last reason write mask).

## To find out

Whether the HAL publishes 32-bit texture formats on the Mach64 and what
it programs for them (the engine has an ARGB8888 texel format), and how
an ADD blend (ONE, ONE) reaches the Mach64's blend factors. Run the
Rage IIC and the Rage Mobility-M through the same two tests.
Record: [decision](../decisions/2026-11-26-1.0-ragexl-a8u4i5.md).

## Update 2026-11-28: cause found, fix not yet run on this card

The 32-bit layout that draws black here is one this engine does not
publish, and CanCreateSurface accepted it, so the texture was created
and sampled as nothing. Since the 2026-11-28 tree, CanCreateSurface
refuses an unpublished 32-bit RGB video-memory texture with
`DDERR_INVALIDPIXELFORMAT`, and T22 should be skipped for that layout
here. Measured on the 86Box ViRGE only, where T22 went from failing to
skipped: [decision](../decisions/2026-11-28-unpublished-32bit-textures-refused.md).
Close this issue on a V9XD32 run on this card.
