# GDI screen-to-screen copies on the Gen3 blitter

Date: 2026-11-24. Status: built and measured on the GMA 950 netbook:
copies 13 to 47 times faster than the DIB Engine and pixel-exact
(`docs/decisions/2026-11-24-gen3-gdi-copies-and-text-on-the-netbook.md`).
Copies past 128 bands run as several streams since a census of Internet
Explorer found its long scrolls going to the CPU
(`docs/decisions/2026-11-24-ddi-census-ie-on-the-netbook.md`), confirmed
on the netbook (`docs/decisions/2026-11-24-ddi-census-netbook-multi-stream-copies.md`).
Text was built on the blitter too, measured 3 to 5 times slower than the
CPU, and is not part of this change (decision 9).

GDI on the Gen3 family was the DIB Engine on the CPU. Its fills run near
the engine's speed through the write-combined aperture, 1.76 against 1.52
cycles a pixel, but a screen-to-screen copy reads uncached memory: 130
cycles a pixel against about 3 on the engine
(`docs/decisions/2026-10-25-intel-gen3-write-combining.md`). A window
dragged, a list scrolled and a text window scrolled are all
screen-to-screen `SRCCOPY` BitBlts. That is the one primitive this plan
moves to the blitter. Linux's SNA does the same copies with
`XY_SRC_COPY_BLT` (`sna_blt.c`).

## What was already there

- At Enable, with 3D permitted, the DRV maps BAR0 and the GTT and opens the
  ring at the reserve (`gma950_hw16.c`, `V9xMiniI9xxRingOpen`), before any
  DirectDraw program loads the HAL.
- The blit builders and their decoder are pure, host-tested C
  (`i9xx_blt.c`), and the ring plan is in the DRV (`i9xx_ring.c`,
  I9XXCODE).
- DirectDraw holds the Win16 mutex around every HAL callback that was
  sampled (`2026-09-26-98se-directdraw-holds-the-win16-mutex-...md`), and
  the HAL reads TAIL and HEAD from the registers on every submit. GDI's
  display driver entries run under the same mutex. So a second producer
  that also reads TAIL and HEAD each time is serialized with the HAL. That
  rests on the measurement plus the Win16 design and has not been tested
  with both producers busy.

## Decisions

1. **Copies only.** Screen-to-screen `SRCCOPY`, source and destination both
   the screen PDEVICE, at 8, 16 and 32 bpp. Fills stay on the CPU: the WC
   numbers above leave nothing to win. Gen3's descriptor claims no 2D
   capability bit (the HAL routes DirectDraw's blits by engine type), so
   `v9x_gdi_accel_configure` gives the Gen3 engine the screen copy alone.
2. **Synchronous.** Each accepted BitBlt writes `MI_FLUSH`, its copies,
   `MI_FLUSH` and a breadcrumb, moves TAIL, and waits for the breadcrumb
   before it returns. Nothing GDI starts is still running when GDI returns:
   no dirty flag, no BeginAccess drain, no handover with the HAL's Lock and
   Flip, and `GdiAccelSync` does not apply. The leading flush puts whatever
   the 3D engine left in its render cache into memory before the blitter
   reads it.
3. **The DRV's own breadcrumb.** A dword at byte `0x100` of the status page.
   The HAL's words at `0x80` (breadcrumb, allocator, magic pair) are not
   read or written. That keeps the HAL's proving and allocator rules
   exactly as they are.
4. **Overlap by bands.** Gen3's blit has no scan-direction control
   (`i9xx_blt.c` refuses overlap). An overlapping copy is split into bands
   that each do not overlap their own source, ordered so a band never reads
   what an earlier band wrote. The ring runs them in order on one engine.
   Vertical moves use bands of at most |dy| rows; a pure horizontal move
   uses bands of at most |dx| columns. A stream holds 128 bands; a copy
   that needs more is several streams, each submitted and waited for in
   order, up to 2,048 bands, and only when the bands average at least 256
   pixels (`V9X_I9XX_GDI_MIN_BAND_PIXELS`, an estimate of where the CPU
   stops being faster, not a measured crossover). Whether the blitter's own
   scan order would make some overlaps safe in one packet is not measured;
   the bands do not depend on it.
5. **Gates** are `gdi_accel.c`'s, as for every other engine: enabled and
   not poisoned; source and destination the screen PDEVICE with `DE_VRAM`
   and without `DE_BUSY`; depth; extents inside the screen; the area at
   least `GdiAccelThreshold` (1,024 pixels by default). At gate 8 the band
   plan and the decoder build the first stream; a copy they refuse declines
   before the cursor is excluded. The cursor is excluded with
   `deBeginAccess` and `CURSOREXCLUDE` over the union of both rectangles,
   as for the S3 and Matrox copies.
6. **Off switches.** `[Velocity9x] GdiAccel=0` (or `GdiAccelCopy=0`) in
   SYSTEM.INI. `V9X3D OFF` from DOS turns it off with the ring.
7. **Failure.** A ring that has no space within the bound, an invalid ring
   plan or a breadcrumb that does not arrive latches `gdi_accel.c`'s poison
   for the session (`GdiAcceleration=gdi-poisoned`). A copy that failed in
   its first stream is redone by the DIB Engine. One that failed in a later
   stream is reported done, because its source may be half moved and the
   DIB Engine cannot start it again; a timeout part-way through one stream
   already meant the same.
8. **Errata.** Blits were authorized on 27AE rev 03 by the 2026-09-25 gate,
   and erratum 12 (CPU and GPU memory interleave, "may hang") is open on
   A3. GDI copies interleave CPU and GPU access more densely than DirectDraw
   blits. The netbook ran them through a day of use with this risk
   accepted, recovery being `GdiAccel=0` or `V9X3D OFF`.
9. **No text.** `ExtTextOut` through `DIB_ExtTextOutExt` with
   `XY_MONO_SRC_COPY_IMMEDIATE_BLT` packets was built and drew
   pixel-exact, but each callback costs a submission and a wait, and the
   CPU expands text into write-combined memory near the engine's speed:
   on the netbook the CPU was 3 to 5 times faster. It is left out.

## Where it lives

`src/chipsets/intel/i9xx_gdi.c` is the band plan and the stream, pure C
with host tests that run every band order against a simulated
framebuffer. `src/display16/gdi_gen3.c` submits and waits;
`gdi_accel.c`'s Gen3 arm, under `V9X_INTEL_GMA_FAMILY`, configures and
calls it. The ring, register and aperture primitives are `runtime.asm`'s.
All of it but the arm is in LEAFCODE (`velocity9x/leaf16.h`), with the
`i9xx_blt.c` and `i9xx_flip.c` builders it calls near: the family's
`_TEXT` had 517 bytes left and I9XXCODE was full. LEAFCODE cannot reach
the C runtime's 32-bit multiply and divide, which live in `_TEXT`, so
every multiply there has 16-bit operands; a 32-bit one fails the link
(E2052), which was checked by planting one.

## Measure

V9XGDIB.EXE, a Win32 tool that times screen-to-screen BitBlts and
ScrollDC at several sizes against a CPU-made reference with the copies on
and off, measured the netbook (the decision record above). It is not part
of this change.
