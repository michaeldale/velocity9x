# Rage 128 Pro: V9XD32's X8R8G8B8 texture draws black

Date: 2026-11-27. Machine: A8U4I5, Rage 128 Pro (Rage Fury Pro/Xpert
2000 Pro, `1002:5046`, subsystem `1002:0018`), the 0.61.0 tree's `ati`
set. Status: open, cause not looked at. The first V9XD32 run with T22
on this card, so there is no earlier result to compare. Left for a
later release (Michael, 2026-11-27); a README known issue in 1.0.

## What is seen

V9XD32 fails one check at all three DDI levels (default, `/ddi6`,
`/ddi5`, 32 bpp with a 16-bit Z;
[report](../probe/rage128pro-1.0-2026-11-27/rage128pro/v9xd32-default-V9XD32.TXT)):
T22's X8R8G8B8 texture is created, locked, given a handle and drawn
with every `Hr` 0, and all four judged pixels read 0 (`T22XOk=0`). The
A8R8G8B8 texture beside it passes, its four quadrants exact
(`T22APixel0=002040C0` and so on). Every other test passes or is
skipped for a missing capability.

The SiS 6326 fails the same check the same way
([issue](2026-11-27-sis6326-v9xd32-x8r8g8b8-texture-draws-black.md)),
on a different engine, which points at something both share above the
engine (the format the runtime or the HAL hands down for X8R8G8B8, or
how its unused byte is treated) as much as at either one; not checked.

## To find out

What texel format the HAL programs for X8R8G8B8 on the Rage 128 (it
has an xRGB8888 texel format distinct from ARGB8888), and whether the
SiS 6326 and the Rage 128 take the same path to it. Record:
[decision](../decisions/2026-11-27-1.0-rage128pro-a8u4i5.md).

## Update 2026-11-28: cause found, fix not yet run on this card

The 32-bit layout that draws black here is one this engine does not
publish, and CanCreateSurface accepted it, so the texture was created
and sampled as nothing. Since the 2026-11-28 tree, CanCreateSurface
refuses an unpublished 32-bit RGB video-memory texture with
`DDERR_INVALIDPIXELFORMAT`, and T22 should be skipped for that layout
here. Measured on the 86Box ViRGE only, where T22 went from failing to
skipped: [decision](../decisions/2026-11-28-unpublished-32bit-textures-refused.md).
Close this issue on a V9XD32 run on this card.
