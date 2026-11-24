# Internet Explorer on the netbook: 3.3 % of the time in the display driver, most of it 20 scrolls the blitter refused

Date: 2026-11-24. A build of the author's Gen3-only branch with the GDI
copies (`docs/plans/gen3-gdi-blitter.md`), at most 128 bands a copy, and a
DDI census. Machine: the GMA 950 netbook (945GSE, Atom, TSC 1,663 MHz),
Windows 98 SE. Report:
[V9XCENS.TXT](2026-11-24-ddi-census-netbook-ie-v9xcens.txt).

The census was a diagnostic and is not part of this change: the DIB
Engine forwards in `dib_thunks.asm` timed with the TSC per DDI entry, and
BitBlt and ExtTextOut timed in C, BitBlt also counted by kind (pattern,
screen or memory source and destination), by ROP3 and by whether the
blitter took it. V9XCENS.EXE read it through an escape before and after
the user's session and wrote the difference.

## What was run

The owner installed the census build, ran V9XCENS.EXE, used Internet
Explorer for 52.8 seconds, and pressed OK. The pages and the screen mode
were not recorded. "Not timed (too deeply nested): 0" in both sections;
the owner saw nothing wrong with the census build in use.

## What it says

- **1.725 s of 52.8 s, 3.27 %, was in the display driver.** The rest is
  IE, the network and everything else. No change to the driver can make
  IE as a whole more than about 3 % faster on this workload.
- **BitBlt was 1,497 ms of that, 87 %.** StretchDIBits 133 ms,
  ExtTextOut 63 ms (7.5 us a string, on the CPU as decided that morning),
  DibToDevice 11 ms, the rest under 10 ms together.
- **20 screen-to-screen SRCCOPYs the blitter did not take: 1,010 ms**,
  59 % of all the driver's time, 50.5 ms each for 321,000 pixels each
  (6.4 Mpixel/s, the CPU's uncached-read rate). 141 the blitter did take
  averaged 1.36 ms for 308,000 pixels.
- The other kinds are small: 8,842 PATCOPY fills to the screen, 69 ms
  (7.9 us each); PATCOPY into memory bitmaps 52 ms; memory-to-screen
  SRCCOPY 51 ms; the SRCAND and SRCINVERT pairs of masked icons, 76 ms
  for about 230 pixels a call; four screen-to-memory SRCCOPYs 31 ms.

## Why the 20 were refused

Their size says whole-window copies, and the only gate a large, aligned
screen-to-screen SRCCOPY fails is the band plan's: an overlapping copy is
cut into bands no taller than the move, and more than 128 bands
(`V9X_I9XX_GDI_MAX_BANDS`) was refused. A window of 300 rows or more
scrolled by one or two rows needs 150 to 600. The census does not record
the move, so this is inferred from the counters (`decline_overlap` is the
GDI counters' "bands", which V9XGDIB showed at 25 before its first run),
not read from the copies themselves.

## Decision

Copies past 128 bands go to the blitter as several streams of up to 128
bands, submitted and waited for in band order, up to 2,048 bands a copy,
when the bands average at least 256 pixels
(`V9X_I9XX_GDI_MIN_BAND_PIXELS`). The 256 comes from the first V9XGDIB
run: a band cost the blitter about 15 to 40 us, and the CPU moves about 6
pixels a microsecond. It is an estimate of the crossover, not a
measurement of it. A 300-row scroll by one row of an 800-pixel window is
300 bands; at that cost about 5 to 12 ms against 50 on the CPU.

Not pursued, from the same report: the fills, uploads and masked icons
are CPU writes to write-combined memory, or reads of a few hundred
pixels, where a submission and a wait cost more than the work.

## Not known

- Whether the 20 were scrolls by one or two rows; the next census run
  says, by whether they move to the blitter's line.
- What a band costs at one row high; the estimate is from bands of 5 to 8.
