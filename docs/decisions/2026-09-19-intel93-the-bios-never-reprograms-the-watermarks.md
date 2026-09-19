# intel93: the watermark is a third of what the arithmetic asks for

> **Corrected 2026-09-20.** This document was filed as "the BIOS never
> reprograms the watermarks", and the headline did not follow from the
> capture. Both modes report the same pixel rate, the same pixel format and
> the same FIFO partition, so a correct per-mode calculation would produce
> the same number twice: what four identical readings establish is
> "unchanged across these captured transitions", not "never reprogrammed"
> and not "wrong for every other mode". The plane A figure of 17 was also
> wrong - see below. The measurement that stands is plane B's, and it is
> the one that matters.


2026-09-19, MICHAEL-NETBOOK (945GSE), build `5d9fbf6-dirty`, Final Reality
Robots. The last capture of the flicker investigation. Attached:
`2026-09-19-intel93-V9XSNAP.txt`.

## Four entries, two modes, one watermark

```
Wm0Src=0x03FF023F (1024x576)  Wm0Blc=0x03060106  Wm0Dsparb=0x00001D9C
Wm1Src=0x027F01DF ( 640x480)  Wm1Blc=0x03060106  Wm1Dsparb=0x00001D9C
Wm2Src=0x03FF023F (1024x576)  Wm2Blc=0x03060106  Wm2Dsparb=0x00001D9C
Wm3Src=0x027F01DF ( 640x480)  Wm3Blc=0x03060106  Wm3Dsparb=0x00001D9C
```

The log's new trigger works: four entries, alternating the panel's mode and
the game's as the run enters and leaves Final Reality.

**FW_BLC is identical in all four**, and so is DSPARB, and so is the pixel
rate: both modes run the pipe at 54,180 kHz at 16 bpp. A per-mode
calculation would therefore be entitled to land on the same value twice.
The reading establishes what FW_BLC HOLDS while the game runs. It does not
establish where that value came from, and the earlier draft of this document
was wrong to say it did.

What the value is remains the point, and it does not depend on provenance.

## What it should be

`DSPARB` reads `0x00001D9C` throughout. Against i915's layout - BSTART in
bits 6:0, CSTART in 13:7 - that is plane A 28 entries and plane B 31. At
the pipe's 54,180 kHz and 16 bpp, `intel_calculate_wm` gives:

| | programmed | computed | |
|---|---|---|---|
| plane A watermark | 6 | **26** | idle: FIFO less the guard |
| plane B watermark | 6 | **20** | live: drives the panel |

Plane B is the one scanning out. Plane A is disabled in this capture, and an
idle plane does not get a watermark computed from a pixel rate it is not
consuming: i915 hands it `fifo_size - guard_size`, 26 here. The first draft
of this document computed plane A as though it were live and got 17, which
is a valid piece of arithmetic about a configuration this machine is not in.

Twenty against six is roughly a third of the margin i915 would allow, on a
pipe that intel90 and
intel91 both measured underrunning. That is a coherent account of the
underrun, and the first mechanism in this investigation that is both
measured and explicable.

It is not yet a proven account of the FLICKER. The underrun is measured,
the watermark shortfall is measured, and the link between the underrun and
the frames the camera caught is inference from the symptom's shape and its
concentration in heavy scenes. Programming the watermarks and re-counting
the dips is what would close it.

## The module had the DSPARB layout wrong, and said so

`Wm0Want` through `Wm3Want` all read `0x00000000`. The first cut of
`i9xx_wm.c` placed BSTART at bit 9 and CSTART at bit 16; against the
netbook's `0x00001D9C`, whose bits above 13 are zero, that gives a CSTART
of nothing, and `v9x_i9xx_wm_fifo_split` declined rather than returning a
partition it could not believe.

So the capture reported a refusal instead of a wrong number, which is the
only reason the error was visible at all. The shifts are corrected, and the
netbook's own register, timing and answer - 0x1D9C, 54,180 kHz, 26 and 20 -
are pinned in `tests\host\test_i9xx_wm.c` so they cannot drift.

Four instruments in this investigation have been wrong in a way that read
as a clean result. This is the first that was wrong in a way that read as
no result, and it cost one capture instead of a conclusion.

## Where the flicker work stops

Closing at the operator's decision, with the state as follows.

Measured and standing: the scanout underruns on the live pipe, twice, with
a clean baseline and boundary; self-refresh is off and not the cause; the
live plane's watermark sits at roughly a third of i915's margin, and does
not change across the transitions captured.

Measured and closed out: premature flip completion, buffer ownership,
draws racing a pending flip, and unfinished drawing at the flip - each with
its own instrument, on both cards. The clear DOES race a pending flip on
the netbook (1,001 of 1,030 Blts), which is a real defect but unexplained
in consequence, because the instrument that would say which buffer it hits
is blind in that window.

Done on 2026-09-20, after this document was first filed: the driver now
programs FW_BLC on a pipe-source change, merging only the two watermarks and
the two burst bits so the BIOS's bit 25 survives, and leaving self-refresh
disabled as found. `WmWrites` and `WmWritten` report whether the write
happened and what the register read back. It is UNCONFIRMED as a fix; no run
has counted dips against it. `docs\issues\2026-09-19-the-flicker-state-of-play.md`
carries the whole picture.
