# The netbook with multi-stream copies: no large scroll left on the CPU; StretchDIBits is now most of the driver's time

Date: 2026-11-24. The census build of
`2026-11-24-ddi-census-ie-on-the-netbook.md` with the copies past 128
bands run as several streams (`docs/plans/gen3-gdi-blitter.md`, decision
4). Machine: the GMA 950 netbook, TSC 1,663 MHz. Report:
[V9XCENS.TXT](2026-11-24-ddi-census-netbook-multi-stream-copies-v9xcens.txt).

## What was run

One V9XCENS.EXE session of 163.6 s. What the owner did in it was not
stated with the file; they had been asked to run Internet Explorer and,
separately, Old School RuneScape's web client.

## What it says

- **Every large screen-to-screen copy went to the blitter.** 267, 87.2
  Mpixels, 682 ms, 2.56 ms each (about 326,000 pixels each). The
  unaccelerated screen-to-screen SRCCOPY line of the earlier run (20
  copies, 1,010 ms, 50.5 ms each) is gone; since boot it holds 14 copies
  of 0 kilopixels, under the size threshold. That is what the streams
  were for; which of the 267 needed more than one stream is not counted.
- **10.6 s of 163.6 s, 6.49 %, in the driver**, against 3.27 % in the IE
  run. **StretchDIBits is 9.31 s of it**: 7,315 calls, 1.27 ms each,
  about 45 a second over the session. In the IE-only run it was 1,482
  calls at 90 us. A program drawing frames through StretchDIBits at that
  rate is what this looks like; the census does not record the call's
  sizes, depths or stretch, so neither which program nor why each call
  costs 1.27 ms is known.
- BitBlt fell to 1,157 ms over three times the time.

## What follows

StretchDIBits reads the caller's bitmap from system memory, which the
blitter cannot reach here, and writes the screen, which the CPU does
near the engine's speed; any conversion between the bitmap's format and
the screen's is CPU work in the DIB Engine. At 5.7 % of the session, a
StretchDIBits costing nothing would make the program at most about 6 %
faster. Knowing the bitmaps' depth against the screen's (not known for
this session; the last V9XHW.INI sent was from an earlier boot) is the
next question: a 32-bit frame converted
to 16 bits every time is the one cost a settings change could remove.
