# MGA-2064W: text on the drawing engine

Date: 2026-10-09

Phase 3 of [the Matrox plan](../plans/matrox-millennium-family.md), second
half. The DIB Engine's string bitmaps are expanded onto the screen by the
MGA's ILOAD, and its opaque rectangles are filled by the engine, at 8, 16
and 32 bpp. Text is on by default in the matrox family. The S3 default is
unchanged.

## How it is drawn

`DIB_ExtTextOutExt` lays out the string and calls the driver back with a
monochrome bitmap: `width_bytes * 8` pixels by `height` rows, rows
unpadded, a set bit for a glyph pixel, Windows bit order. The MGA arm draws
it with one ILOAD, using values from the host-tested
`v9x_mga_build_expand`:

- `DWGCTL = 080C6089h`, or `480C6089h` for transparent text: ILOAD,
  linear source, BMONOWF, sgnzero, shftzero, RPL, bop C, and transc to
  skip the background.
  - This is FreeBE's `PutMonoImage` value for a linear source and a
    replace mix. FreeBE runs on the Millennium.
  - For transparent text FreeBE uses BLK, which this driver avoids. RPL
    is allowed with transc by the 1064SG's section 5.5.7.3.
  - The databook's own bit diagram for that section is garbled in the text
    extraction around the `linear` bit, so FreeBE is the authority here.
- `AR0 = W*H - 1` (linear source), with `AR3 = 0` and `AR5 = 0`.
  Section 5.5.7.1 says these "must be 0", and a copy leaves AR5 holding a
  pitch.
- `FXBNDRY` uses an inclusive right edge, as FreeBE writes it.
- `CXBNDRY` is narrowed to the visible columns. Rows above and below the
  clip are not sent at all: the transfer starts further into the bitmap.
- `FCOL` and `BCOL` hold the colours unswapped, for the reason
  `v9x_gdi_virge_text` gives (the DIB Engine's set bit is a glyph).

Before the start, OPMODE `dmamod` is set to DMA BLIT write, with its other
fields kept. Its reset value is general purpose, in which the window's
data would be read as register indices. The bitmap is then written to
DMAWIN (`MGABASE1 + 0`) through the existing `V9xEngineImageRow`, 7 KiB at
a time from offset 0, as FreeBE does. That routine assembles the last
partial dword without reading past the bitmap.

Exactly `v9x_mga_expand_dwords` dwords must follow the start, or the
engine either waits for ever or reads the excess as register writes. The
arm checks that its byte count gives the same dword count before it
writes anything.

After the transfer it waits for idle and writes the setup again. That
reopens CXBNDRY, which the HAL writes only once per mode and would
otherwise draw through.

A timeout poisons as before. For the MGA, recovery writes OPMODE back,
which "will terminate the current DMA sequence" (p.4-66). Whether that
also frees an engine stuck on a short ILOAD is not established: no
timeout has occurred.

## The physical card was approached in steps

Text runs from the first frame of boot, and an ILOAD that stalls the bus
would lock A8U4I5 on every boot. So:

1. A8U4I5's `SYSTEM.INI` had `GdiAccelText=1` left from the S3 work. It
   was changed to `0`, a one-byte edit, before the deploy. Boot 368 came up
   `gdi-fill-copy-overlap`.
2. `V9XGDI /textprobe` armed the driver for exactly one string. Result:
   `ABCDEFGH` (SYSTEM_FIXED_FONT, opaque, 64x15) accepted and drawn on the
   engine, no fallback, no timeout, and the machine still answering.
3. The original `SYSTEM.INI` was put back. Every later boot runs text on.

## Measured

`V9XGDI /accel`, one boot per depth. The 500-operation stream includes 84
text operations. INIs are in
[`docs/probe/a8u4i5-mga2064w-text-2026-10-09/`](../probe/a8u4i5-mga2064w-text-2026-10-09/).

| Machine | Mode | Text drawn on engine | Opaque rects | Text fallbacks | Clip ops accelerated | Injection | Result |
|---|---|---|---|---|---|---|---|
| 86Box Win98SE-Millennium | 800x600x8 | 84 of 84 | 50 | 0 | 48 | poisoned | PASS |
| 86Box Win98SE-Millennium | 800x600x16 | 84 of 84 | 50 | 0 | 48 | poisoned | PASS |
| 86Box Win98SE-Millennium | 800x600x32 | 84 of 84 | 50 | 0 | 48 | poisoned | PASS |
| A8U4I5, physical 2064W (boot 369) | 1024x768x16 | 84 of 84 | 50 | 0 | 48 | poisoned | PASS |
| A8U4I5 (boot 370) | 1024x768x8 | 84 of 84 | 50 | 0 | 48 | poisoned | PASS |
| A8U4I5 (boot 371) | 1024x768x32 | 84 of 84 | 50 | 0 | 48 | poisoned | PASS |

Every run also had 160 fills and 131 copies on the engine,
`IdleTimeouts=0`, `DeclineEngine=0`, and 20 of 20 comparisons against the
reference DC. Boot 372, 1024x768x16 with text on: `V9XDDP` COMPLETE with
the fill and all four overlap cells correct, and the desktop's labels, Start
button and clock all draw.

Outside the harness, the emulator's boot recorded 5 text fallbacks with
reject bit 12: a string at a negative coordinate or past the engine's
coordinate limit. Those are drawn again by the DIB Engine, as designed.

## The harness skipped 32 bpp, and the GDI record overstated it

`V9XGDI /accel` treated 32 bpp as a depth no primitive serves, which is
true of the S3 and was written for it. So it skipped its zero-counter,
clip and fault-injection checks there and still reported PASS.

The [GDI record](2026-10-09-mga2064w-gdi-on-the-engine.md) said every run
poisoned on injection. That was wrong for its two 32 bpp rows. Those runs
did draw on the engine and compare clean (160 fills and 131 copies each),
but the three checks did not run.

The harness now counts 32 bpp as accelerated when the engine is the MGA.
All three 32 bpp runs above ran every check. The GDI record carries a
correction.

## Not established

- **Nobody has looked at the monitor.** Every check reads pixels back
  through GDI.
- **Speed.** Nothing was timed, so no claim is made that text is faster.
- **The Millennium II (2164W).** Same code, not run.
- **Recovery** from a stuck ILOAD, as above.
