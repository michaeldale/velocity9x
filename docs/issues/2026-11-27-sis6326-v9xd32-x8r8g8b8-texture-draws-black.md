# SiS 6326: V9XD32's X8R8G8B8 texture draws black

Date: 2026-11-27. Machine: A8U4I5, SiS 6326 rev 0B (`1039:6326`,
subsystem `1569:6326`, 4 MiB), the 0.61.0 tree's `sis` set,
800x600x16. Status: open, cause not looked at. The first V9XD32 run
with T22 on this card, so there is no earlier result to compare. Left
for a later release (Michael, 2026-11-27); a README known issue in 1.0.

## What is seen

V9XD32 `/bpp16` fails one check
([report](../probe/sis6326-1.0-2026-11-27/sis6326/v9xd32-bpp16-V9XD32.TXT)):
T22's X8R8G8B8 texture is created, locked, given a handle and drawn
with every `Hr` 0, and all four judged pixels read 0 (`T22XOk=0`).
The A8R8G8B8 texture beside it passes (`T22AOk=F`), so the engine
samples 32-bit texels; what differs is the format without alpha.
Every other V9XD32 test passes or is skipped for a missing capability.

The Voodoo4 draws both 32-bit forms black
([issue](2026-11-27-voodoo4-v9xd32-32bit-textures-draw-black.md)).

## To find out

What texel format the HAL programs for X8R8G8B8 on the SiS 6326, and
whether the unused top byte is taken as alpha 0 and blended or tested
away. Record: [decision](../decisions/2026-11-27-1.0-sis6326-a8u4i5.md).

## Update 2026-11-28: cause found, fix not yet run on this card

The 32-bit layout that draws black here is one this engine does not
publish, and CanCreateSurface accepted it, so the texture was created
and sampled as nothing. Since the 2026-11-28 tree, CanCreateSurface
refuses an unpublished 32-bit RGB video-memory texture with
`DDERR_INVALIDPIXELFORMAT`, and T22 should be skipped for that layout
here. Measured on the 86Box ViRGE only, where T22 went from failing to
skipped: [decision](../decisions/2026-11-28-unpublished-32bit-textures-refused.md).
Close this issue on a V9XD32 run on this card.
