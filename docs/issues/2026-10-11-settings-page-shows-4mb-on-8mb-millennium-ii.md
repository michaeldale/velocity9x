# The Velocity9x settings page shows 4 MB on the 8 MiB Millennium II

Date: 2026-10-11. A8U4I5 (10.0.1.172), Millennium II MGA-2164W, subsys
1200102B, Velocity9x `matrox` at `4adc408` (boot 398).

## Observed

Michael: the Velocity9x display settings page reports the card's video
memory as 4 MB. The card has 8 MiB, and the driver uses all of it:
`V9XHW.INI` on boot 398 reads `VramMeasuredBytes=8388608` beside
`VbeVramBytes=4194304`, and DirectDraw's heap has been sized from the
walk since boot 385
([memory issue](2026-10-10-mga2164w-vbe-reports-half-its-memory.md),
[walk record](../decisions/2026-10-10-mga2164w-memory-walk.md)).

## Cause (by reading the code; not yet reproduced by me)

`tools\diag\settings_status.c` (the page's status, around line 603)
takes the installed memory from two `V9XHW.INI` keys, in order:

1. `VideoMemoryBytes=` with `VideoMemoryStatus=valid`: a register decode,
   written only by a family with a `read_video_memory` hook (the S3
   parts' CR36). The matrox family writes neither key.
2. `VbeVramBytes=`: the VBE BIOS's 4F00h total. On this card that is the
   4 MiB the BIOS under-reports.

`VramMeasuredBytes=`, which `src\chipsets\matrox\mga_hw16.c` has written
since the walk landed, is read by nothing: not the settings page, not
V9XTRACE, not the field report. The page therefore shows the one figure
the walk exists to correct.

## Fix (proposed, not made)

Rank the sources by authority: a valid register decode, then a measured
size (`VramMeasuredBytes=`, when it is a number and not `not-walked`),
then the BIOS figure. Keep the "Unavailable" path for a card with none.
The ranking is pure logic over three strings and belongs in a
host-testable helper, with cases for each source and for a walk larger
and smaller than the BIOS figure.

Also worth checking at the same time: whether anything else that shows
or reports video memory (the field report's hardware section,
`V9XUPD.EXE`) reads `VbeVramBytes=` alone.

## Not known

- Whether the page reads `V9XHW.INI` before or after the walk writes it
  on a first boot; if before, the measured figure would only appear from
  the second boot.
- Which other families would gain from a measured key: only the matrox
  family walks its memory today.
