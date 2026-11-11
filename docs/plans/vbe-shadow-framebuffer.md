# vbe: a system-memory shadow of the screen, so GDI reads stop crossing the bus

Date: 2026-11-09. Status: Phase 1 built 2026-11-11 with option B and run
on an 86Box guest and the i5-4570 ([Phase 1 as built](#phase-1-as-built-2026-11-11));
Phases 2 and 3 not started. Raised by
[GitHub issue 25](https://github.com/michaeldale/velocity9x/issues/25)
(GTX 970: write-combining on, "real impact on graphics performance is
zero", ReverseBlt still slow) and
[issue 15](https://github.com/michaeldale/velocity9x/issues/15)
(FirePro W9000: scrolling down is now responsive, scrolling up is not).
The issue 25 reporter says another VESA driver uses a shadow buffer and
runs fast. That driver was not read and nothing here comes from it.

## Why

Since 0.39.0 (MTRR) and 0.43.0 (PAT) the `vbe` family write-combines the
frame buffer. That made writes fast and left reads where they were. With
no engine, every GDI operation that reads the screen goes through the CPU
reading video memory, and WC memory is not cached for reads. Measured with
dispbench, `-Mode 1024x768x16`:

| MB/s | M-JJN1ZZ off / on | M-D7TFW4 off / on | i5-4570 off / on |
|---|---:|---:|---:|
| FbWrite | 31.7 / 2,513 | 316 / 3,000 | 66 / 11,357 |
| FbRead | 5.3 / 5.3 | 5.8 / 11.3 | 35.4 / 228.8 |
| BitBlt screen to screen 256x256 | 4.7 / 4.7 | 5.9 / 5.8 | 24.4 / 103.7 |

Sources: [`../decisions/2026-11-07-vbe-write-combining.md`](../decisions/2026-11-07-vbe-write-combining.md)
and [`../decisions/2026-11-08-vbe-pat-write-combining.md`](../decisions/2026-11-08-vbe-pat-write-combining.md).
Writes went up 10 to 170 times; screen-to-screen copies barely moved.
Screen-to-screen copies are window moves and scrolling. The software
cursor saves the pixels under it, and raster ops that read the
destination (XOR, masks, AlphaBlend-like paths in the DIB Engine) read it
as well.

A shadow keeps the authoritative copy of the desktop in system memory.
The DIB Engine draws into and reads from that copy, and every change is
then copied to the frame buffer. The extra copy is a write, which is the
fast direction now.

Why scrolling down feels better than scrolling up on the W9000 is not
known. Both are screen-to-screen copies. A guess (copy direction, and
how the DIB Engine walks the overlap) is not a reason to build anything;
Phase 0 measures both.

## What exists

- Every GDI entry point goes through the driver: `dib_thunks.asm`
  forwards most of them to the DIB Engine, and `BitBlt` and `ExtTextOut`
  are C in `gdi_accel.c`. So there is one place per entry point to add
  "after the DIB Engine returns, copy what changed".
- `Enable` builds the screen PDEVICE with
  `V9xCreateDibPDeviceCall(..., MAKELP(v9x_screen_selector, 0u), ...)`
  (`ddi.c`). The screen's bits pointer is a single argument.
- The mini-VDD already allocates and maps memory for the driver
  (`DpmiMap`, the PAT cover call), so locked, shared memory reachable from
  the 16-bit driver and the 32-bit HAL is an extension, not a new layer.
- `V9X_HW16_DEVICE` hooks and the `NOWC.TXT` pattern for an off switch
  used in A/B runs.

## Design

**Shadow.** One block of locked system memory, the visible desktop's
`pitch x height`, allocated by the mini-VDD at `Enable` and freed at mode
change and `Disable`. 1920x1440x32 is 11 MB. It is mapped twice: a
selector for the 16-bit driver and a linear address for the HAL. The
screen PDEVICE's bits point at the shadow, not at the frame buffer.

**Flush.** After each drawing call returns, copy the rectangle it could
have changed from the shadow to the frame buffer. Per entry point:

| Entry point | Rectangle |
|---|---|
| `BitBlt`, `StretchBlt` | destination rectangle, clipped |
| `ExtTextOut` | opaque rectangle, or the clip rectangle |
| `Output` | the clip rectangle; the whole screen when there is none |
| `Pixel` | one pixel |
| `FastBorder` | the border's rectangle |
| `DibToDevice`, `StretchDIBits`, `DibBlt` to the screen | destination rectangle |
| `StrBlt`, `UpdateColors` | the clip rectangle, or the whole screen |
| `SetCursor`, `MoveCursor`, `CheckCursor` | old and new cursor rectangles |

Calls whose destination is not the screen PDEVICE flush nothing. Where an
argument cannot bound the change, flush the clip rectangle or the whole
screen: slower, never wrong. Phase 1 flushes synchronously, before the
call returns, so there is no window where the screen is behind the
shadow. Deferring and merging flushes is Phase 3 and only if measured.

The cursor entry points run at interrupt time, so the flush must touch
no file, allocate nothing and take no lock a drawing call can hold.

**DirectDraw.** The primary surface is the frame buffer, and programs
lock it and write to it directly. That makes the shadow stale. Options,
one to be chosen (decision 2):

- A. The HAL's Lock of the primary hands out the shadow and Unlock
  flushes the locked rectangle (whole surface without one). The HAL's own
  CPU blits to the primary write to the shadow and flush. Fullscreen
  exclusive mode suspends the shadow (GDI does not draw then) and
  marks it invalid; Windows repaints the desktop when exclusive mode ends,
  and that repaint goes through the shadow.
- B. Leave DirectDraw on the frame buffer and, on Unlock of the primary
  or a HAL blit to it, copy that rectangle from the frame buffer back
  into the shadow. Simple, correct, and reads the bus again for those
  rectangles.

Whether Windows really repaints everything after exclusive mode, and
after a full-screen DOS box, has to be checked on a guest before either
option relies on it.

**Layering.** The rectangle arithmetic (clip, union, "whole screen"
fallback, cursor old/new) is pure C in `src/common/`, with a host test
written first. The mini-VDD allocates and maps, and interprets nothing.
The 16-bit wrappers and the HAL apply it. No register is touched.

**Scope.** The `vbe` family only. Families with an engine read through
the engine or have their own paths; whether they would gain is not
examined here. Decided 2026-11-11 (Michael): the shadow stays `vbe`-only.
`intel-gma`, `ati` and `sis`, which draw GDI with the CPU today, get
their GDI work done properly on the chip later rather than this shadow.

## Phase 0 status (2026-11-10)

Most of the GDI baseline exists already, in dispbench's 2026-11-07/08
`vbe` runs (`dispbench\docs\results\<config>\<stamp>`, 1024x768x16; DBGDI
measures screen-to-screen at sizes from 8x8 to full screen, overlapping
copies, ScrollDC, ScrollBitBlt and screen-to-bitmap). MB/s, 256x256:

| Config, WC | FbWrite | FbRead | ScreenToBitmap | ScreenToScreen | ScreenToScreenOverlap |
|---|---:|---:|---:|---:|---:|
| i5-4570, off (boot 191) | 66.0 | 35.4 | 35.3 | 24.4 | 23.9 |
| i5-4570, on (boot 190) | 11,357 | 228.8 | 148.8 | 103.7 | 23.9 |
| M-JJN1ZZ R3E, off | 31.7 | 5.3 | 5.3 | 4.7 | 4.7 |
| M-JJN1ZZ R3E, on | 2,513 | 5.3 | 5.3 | 4.7 | 4.7 |
| M-D7TFW4 HD 8570, off | 315.7 | 5.8 | 5.8 | 5.9 | 5.9 |
| M-D7TFW4 HD 8570, on | 3,000 | 11.3 | 5.8 | 5.8 | 5.8 |

Two things the plan did not know. On the AMD machines every read-bound
copy sits at the read ceiling, 5-6 MB/s, with WC on or off. On the 4570,
WC lifted the plain copy fourfold and left the overlapping one, which is
what scrolling does, where it was: 23.9 MB/s either way. The same
machine's system-memory DIB-to-DIB copy is 8,679 MB/s, so a shadow turns
a read-bound copy into one at memory speed plus a WC flush.

Still owed from Phase 0: overlap in both directions (DBGDI's Overlap row
is one direction; issue 15's "up is slower than down" may be exactly
that), and the issue 25 GLQuake present time. Both answered 2026-11-11, in
[Phase 1 as built](#phase-1-as-built-2026-11-11).

## Phase 1 as built (2026-11-11)

Four departures from the design above, each for a reason found in the
code:

- **The copy is driven by deBeginAccess, not by a table per entry point.**
  The DIB Engine brackets its screen work with the PDEVICE's
  `deBeginAccess(device, left, top, right, bottom, flags)` and
  `deEndAccess(device, flags)`, the callback it also lifts the software
  cursor with (`98DDK\src\display\mini\s3v\ACCESS.ASM:55-120`), so the
  rectangle has to bound what it touches. `shadow16.c` collects the
  rectangles of a bracket and copies their union when the outermost one
  ends. One hook covers every entry point, including ones the table did
  not list. The rectangle is read as inclusive, which costs a row and a
  column if it is not. The one exception measured is `Output`, whose
  bracket spans the whole screen (a whole-screen copy per line on the
  4570). It is the table's row for `Output`, made exact: the call is
  bounded by its own points (OS_POLYLINE, OS_SCANLINES, OS_POLYSCANLINE),
  and a scanline series' begin and end, which draw nothing, by an empty
  rectangle.
- **The cursor thunks copy the cursor.** The DIB Engine draws the software
  cursor into the shadow from the mouse interrupt, outside any bracket.
  `SetCursor`, `MoveCursor` and `CheckCursor` in `dib_thunks.asm` call the
  DIB Engine and then copy the old and new cursor rectangles (position
  less hot spot, `WINDEFS.INC:110-116`).
- **The memory is DPMI, allocated by the 16-bit driver.** 0501h, locked
  with 0600h, one LDT descriptor (`runtime.asm`, `V9XSHADOWALLOC`). The
  DirectDraw shared block is the same kind of memory reached the same two
  ways, so the mini-VDD needs no new service. The HAL learns the linear
  address from `V9X_DD_SHARED.shadow_linear`.
- **Option B, but blits do not read back.** A CPU blit to the visible
  primary draws into the shadow, reading a visible source from the shadow
  too, and copies the rectangle out; a fill fills both. Only an Unlock of
  the primary reads back, the locked rectangle, or the whole surface
  without one. All of it is skipped while exclusive mode keeps GDI off
  the screen (`v9x_present_gdi_may_draw`).

Also: a mode restore (`ResetHiResMode`, the unchanged-mode `ReEnable`)
copies the whole shadow out, so the desktop is back before USER repaints;
a copy is all that nested VDD callback may do. Decision 3: the PDEVICE
keeps `V9X_DE_VRAM`, since the bracket is what says what to copy; the
86Box run shows the bracket arriving with it set. The off switch is
`C:\V9XDIAG\NOSHADOW.TXT` (decision 1); V9XHW.INI `ScreenShadow=` says
`on`, `off-marker` or `alloc-failed`.

Results ([record](../decisions/2026-11-11-vbe-screen-shadow-phase1.md)):
on the i5-4570 against the off marker, screen-to-screen 256x256 went from
103 to 2,124 MB/s, the overlapping copy from 23.8 to 2,665, ScrollDC from
153 to 6,957. Write-only operations run at 0.3 to 0.8 of their speed, text
costs 1.3 times as much and lines 1.35 to 1.9 times. On 86Box the frame buffer
matched the shadow after GDI, cursor, DirectDraw, exclusive mode and
full-screen DOS exercises. Phase 1's gate is met on one machine; the AMD
machine it also asks for has not been run, and nor has a windowed Lock of
the primary.

Later the same day ([record](../decisions/2026-11-11-vbe-screen-shadow-open-items.md)):
the R3E reads 75 to 250 times faster at 8, 16 and 32 bpp, writes at 0.32 to
0.73. Phase 1's gate is met on both machines. A windowed Lock of the
primary and windowed OpenGL presents reach the shadow on 86Box, and 8 bpp
compares clean there. Phase 0's two owed items are closed. Scrolling: the
4570 without the shadow scrolls up a document 7 times slower than down
(issue 15's report), and with it 65 to 360 times faster either way. The
GLQuake present reads a back buffer the software renderer keeps in video
memory, which is its own issue
([`../issues/2026-11-11-vbe-soft-gl-back-buffer-in-video-memory.md`](../issues/2026-11-11-vbe-soft-gl-back-buffer-in-video-memory.md)).

## Phases

0. **Baseline.** On two `vbe` machines (one AMD, the i5-4570): dispbench
   `DBGDI` screen-to-screen at several sizes, FbRead, and a timed scroll
   up and down in a long Notepad file. Also explain the issue 25
   GLQuake log first: windowed software GL there spent `present-ms=5895`
   on 4 swaps (about 1.5 s each), where other soft-GL logs in
   `docs/probe` show 4 to 7 ms across 47 to 90 swaps. If that present reads
   the screen, the shadow helps it; if it is something else, it is its
   own issue.
1. **GDI shadow, synchronous flush.** The table above, DirectDraw not yet
   using the shadow; while a DirectDraw primary is locked or exclusive,
   option B's copy-back keeps it correct. Gate: `run-checks.ps1`, V9XGDI
   intact on both machines, dispbench A/B against the off marker.
2. **DirectDraw on the shadow** (option A, if decision 2 picks it).
   Gate: V9XDDP, a windowed DirectDraw game and a fullscreen one, the
   desktop intact after each and after a full-screen DOS box.
3. **Deferred flush,** only if Phase 1 shows the synchronous copies
   costing more than they save on small, frequent calls (text, cursor).

## Decisions owed

1. **Off switch.** A marker file (`C:\V9XDIAG\NOSHADOW.TXT`, as
   `NOWC.TXT`) for A/B runs and as a user's way out. The shadow itself is
   the default, not an option. Built so in Phase 1.
2. **DirectDraw**: A or B above. B for Phase 1 (Michael, 2026-11-11).
3. **`V9X_DE_VRAM`.** Whether the screen PDEVICE keeps that flag once its
   bits are in system memory. Kept in Phase 1 (above). DIBENG.INC defines
   it as "physical surface (video memory)" and says nothing about what
   the DIB Engine does with it; whether clearing it would stop the
   deBeginAccess bracket was not tried.

## Done when

- Screen-to-screen BitBlt on a `vbe` machine improves against the same
  machine with the off marker, by a measured amount; FbWrite-bound rows
  do not get worse by more than the extra copy accounts for.
- Scrolling up and down in Notepad, and dragging a window, are timed
  before and after.
- V9XGDI and V9XDDP pass, and the desktop is intact after DirectDraw
  games, exclusive mode and a full-screen DOS box.

## Not known

- The cost of a second copy for every small GDI call. On machines where
  WC writes are slow (the Pentium III's 240 MB/s) the shadow could lose on
  text-heavy work.
- Whether any program reads the primary through DirectDraw expecting
  pixels GDI drew a moment ago. Option A covers it; B covers it at
  bus-read speed.
- Windows 95.
