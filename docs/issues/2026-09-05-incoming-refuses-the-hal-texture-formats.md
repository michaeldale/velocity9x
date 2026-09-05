# Incoming (1998) cannot create a texture on the hardware path: no format it wants is enumerated

Date: 2026-09-05
Status: **open**. Reproduced twice on A8U4I5 (Trio3D/2X, HEAD's pair, boot
41); not yet run under software Direct3D, which is the experiment that names
the missing format.
Title: *Incoming: The Final Conflict*, Rage Games, demo build of 1998-03-17
(`archive.org/details/incoming_201401`), installed at `C:\INCOMING`.

## Observed

Launched as `incoming.exe -primary -nocd`, the game opens a 640x480 window,
draws nothing, and reports:

> CreateSurface for texture failed (loadtex).
> The pixel format was invalid as specified.

OK exits the game cleanly. `-alpha` ("force use of alpha texture formats", per
the readme) fails identically. Nothing reached the display in either run.

## Where the refusal comes from

Not from the HAL. `V9xHalCanCreateSurface` and `V9xHalCreateSurface` in
`src/display32/ddhal_core.c` accept every request and return `DD_OK`;
neither inspects a pixel format. `DDERR_INVALIDPIXELFORMAT` on a texture is
DirectDraw's own answer when the requested format is not in the HAL's
`lpTextureFormats` list, which on the S3D path is exactly two entries:
ARGB1555 and ARGB4444 (`src/display32/d3d/d3d_virge.c`). So the game asked for
a third format and was refused before the driver saw it.

Which format is not established. The two candidates, in order:

- **RGB565 without alpha**, the format of the primary the game was given, and
  the format a 1998 engine reaches for first for opaque textures. The S3D
  unit has no 565 texel mode (`08bc83f`), which is why the hardware path
  does not offer it; the software engine does (`d3d_soft.c` enumerates
  1555, 4444 and 565).
- **8-bit palettised**, common in that era's engines. The S3D unit *does*
  have palettised texel modes, which this HAL has never exposed.

## The experiment that decides it

Set `Direct3D=2` (software) in the driver's SYSTEM.INI section, restart, and
launch the same command line. If the game starts, 565 was the missing format
and the S3D path needs a 565 texture entry - which means an upload-time
conversion to 1555, since the texel unit cannot sample 565. If it fails the
same way under software mode, 565 was not what it wanted and palettised is
next. Not run yet: a restart on this machine can also move the blend state,
so it is a decision to take deliberately rather than in passing.

## Context

The run was taken with the card in the bad blend state (`TexMatrixOk=90`,
`trio3d-a8u4i5-v9x-2026-09-05j-b41-bad.ini`). That is irrelevant to this
failure, which happens before any triangle is drawn, but it is recorded
because every Trio3D result is.

3DMark 99 and Final Reality both run on this path, so whatever Incoming asks
for, those two do not.
