# The vbe screen shadow, Phase 1: 256x256 screen-to-screen copies 21 and 112 times faster on the i5-4570, writes 0.3 to 0.8 as fast

Date: 2026-11-11

Status: implemented on the `vbe` family. Measured on one machine (W9QI5A83, i5-4570, HD 4600 under the
`vbe` package, write-combined through PAT), A/B against the off marker in
consecutive boots; correctness checked on one 86Box guest. Not run on an
AMD machine, which is where the field reports come from.

Plan: [`../plans/vbe-shadow-framebuffer.md`](../plans/vbe-shadow-framebuffer.md)
(Phase 1 with DirectDraw option B, Michael, 2026-11-11). Field reports:
[GitHub issue 25](https://github.com/michaeldale/velocity9x/issues/25) and
[issue 15](https://github.com/michaeldale/velocity9x/issues/15).

## The change

The screen PDEVICE's bits point at a copy of the desktop in locked system
memory, and what GDI draws is copied to the frame buffer:

- `src\common\shadow.c` (host-tested, `tests\host\test_shadow.c`) does the
  rectangle arithmetic: unions, clipping, the dword-widened byte spans,
  cursor rectangles, and the bounds of an `Output` call.
- `src\display16\shadow16.c` keeps the state. The PDEVICE's
  `deBeginAccess`/`deEndAccess` collect the DIB Engine's rectangles and
  copy the union when the outermost bracket ends. The cursor thunks copy
  the cursor's old and new rectangles. `Output` is bounded by its points.
- `runtime.asm` allocates the block (DPMI 0501h, locked with 0600h, one
  LDT descriptor) and copies rows with 32-bit string moves.
- `ddi.c` gives the PDEVICE the shadow's selector unless
  `C:\V9XDIAG\NOSHADOW.TXT` exists, and copies the whole shadow out after
  a mode restore (`ResetHiResMode`, the unchanged-mode `ReEnable`).
  V9XHW.INI `ScreenShadow=` records `on`, `off-marker` or `alloc-failed`.
- The HAL (`ddhal_core.c`, `blt_cpu.c`) learns the address from
  `V9X_DD_SHARED.shadow_linear`. A CPU blit to the visible primary draws
  into the shadow and copies its rectangle out; a fill fills both; an
  Unlock of the primary reads the locked rectangle back. None of it while
  exclusive mode keeps GDI off the screen.

## What the measurements killed

**"The deBeginAccess bracket bounds every drawing call."** It bounds
`BitBlt`, fills and text, but not `Output`. The first shadow build spent
452,000 cycles on every line, against 3,400 to 6,900 without the shadow:
one whole-screen copy per line. `Output` is now bounded by its own points
(OS_POLYLINE, OS_SCANLINES, OS_POLYSCANLINE). With that, lines and
Rectangle came back.

**"Ellipses and polygons arrive as OS_POLYSCANLINE."** Bounding that style
changed nothing: Ellipse stayed at 0.17 of the shadow-off speed, Polygon
at 0.09, RoundRect at 0.11, each still one whole-screen copy per shape.
Giving OS_BEGINNSCAN and OS_ENDNSCAN an empty bound (they draw nothing;
the s3v sample's `BeginScan_S3` only prepares,
`98DDK\src\display\mini\s3v\SCANLINE.ASM:819-833`) brought them to 0.64
to 0.74. So GDI fills those shapes as a scanline series that the DIB
Engine brackets with the whole screen. That is inferred from the timing;
no call was traced.

The DIB Engine's GDIINFO on this family claims LINECAPS 0x23 and
POLYGONALCAPS 0x08 (DBCAPS), with no wide lines and no curves, which is
why those styles are the only ones that draw.

## Measurements, i5-4570, 1024x768x16, write-combined

dispbench DBGDI and DBDD, build `a80e784`, driver `dd450a5f-dirty` (this
work, uncommitted). Shadow off is boot 202 with `NOSHADOW.TXT`; shadow on
is boot 205 with the final build. MB/s:

| Operation | Off | On | On/off |
|---|---:|---:|---:|
| BitBlt screen to screen 256x256 | 103.2 | 2,123.5 | 20.6 |
| BitBlt screen to screen, overlapping 256x256 | 23.8 | 2,664.8 | 112 |
| BitBlt screen to bitmap 256x256 | 148.0 | 3,123.1 | 21.1 |
| ScrollDC | 153.2 | 6,956.7 | 45.4 |
| SRCAND 64x64 (reads the destination) | 14.3 | 559.6 | 39.1 |
| FillRect 256x256 | 4,210.6 | 3,198.5 | 0.76 |
| PatBlt hatch 256x256 | 9,634.4 | 2,906.9 | 0.30 |
| BitBlt DIB to screen 256x256 | 4,168.7 | 2,077.1 | 0.50 |
| SetDIBitsToDevice 16 bpp 1024x768 | 4,234.2 | 3,054.2 | 0.72 |
| Ellipse 128x128 | 851.0 | 633.9 | 0.74 |
| Polygon 128x128 | 1,094.6 | 701.8 | 0.64 |

Cycles per call, lower is better:

| Operation | Off | On |
|---|---:|---:|
| ExtTextOut fixed, opaque | 19,863 | 25,781 |
| LineHorizontal | 3,376 | 4,564 |
| LineDiagonal30 | 4,787 | 9,304 |

Everything that reads the screen is faster: 23 to 162 times at 64x64 and
larger, 2.5 to 34 times at 8x8 to 32x32, where the call overhead counts
for more. That is scrolling, moving windows, and every ROP that reads
the destination.
Everything that only writes is 0.3 to 0.8 as fast, which is the second
copy the plan expected. Text costs 1.3 times as much, lines 1.35 to 1.9
times.

DirectDraw (DBDD) runs exclusive, where the shadow does not apply. Its
rows matched shadow-off within 6 % in the first shadow run (boot 201).
In the final run (boot 205) a few small blits read 0.26 to 0.73, with
the same HAL code. That is recorded once as run-to-run variance and was
not rerun.

Whole tables: [`../probe/vbe-shadow-4570-2026-11-11/`](../probe/vbe-shadow-4570-2026-11-11/)
(`gdi-shadow-off-vs-on.md`, `dd-shadow-off-vs-on.md`, every run's
reports). The intermediate builds are there too: `shadow` (no `Output`
bound), `shadow-out` (polylines and scanlines), `shadow-scan` (plus
poly-scanlines). Afterwards the machine went back to its boot-200
intel-gma files (boot 206, hashes checked).

**One DBGDI death, not explained.** On boot 203 (`shadow-out`), DBGDI
wrote every section and died before its final `[Result]`. That is after
it destroys its full-screen window, where the desktop repaints through
`Output`. The run started 76 s after boot, with startup programs still
loading. The same build's rerun completed, and so did the two later
builds' runs, which started 3 to 4 minutes after boot. No error dialog
was on the screen afterwards.

## Correctness, 86Box (Win98SE-Mach64VT2, Mach64 VT2, Pentium MMX)

The emulator's own capture of its window (PrintWindow) is what the CRTC
scans out: the frame buffer. The agent's screenshot is a GDI read: the
shadow. A copy the driver missed shows as a difference between the two.
Compared after:

- boot to the desktop;
- an Explorer window opened, its file list scrolled, the window dragged
  partly off screen, and the Start menu;
- V9XGDI `/auto` (PASS);
- V9XDDP: exclusive mode at 640x480 and back (Result=COMPLETE);
- a full-screen DOS box and back. The desktop is in the frame buffer one
  second after the return, before USER's repaint;
- V9XDDF `/primary` (PASS): DirectDraw fill, lock, read and copy in the
  primary's corner. The shadow holds the same corner, which without the
  HAL's half it would not;
- Paint: a pencil stroke, two ellipses, a rectangle;
- with the final build, a scratch tool drawing an ellipse, round rect,
  pie, chord, a winding star, a dashed polyline and a line straight to
  the screen.

Every comparison differs only at the cursor (a GDI read lifts it) and the
remote agent's tray icon, which turns green while it serves the request.

The committed build adds one thing after the 4570 runs: an `Output` call
whose destination is not the screen PDEVICE (a device bitmap, such as
the ones Paint draws into) passes straight through, unbounded and
uncopied. Before, it added a screen rectangle to copy: never wrong, only
wasted work. That build was rechecked on this guest (boot 154) with Paint
and the shapes tool, and showed the same two differences only. It was
not rerun on the 4570.

Captures, diffs, the shapes tool's source and the guest's records:
[`../probe/vbe-shadow-86box-2026-11-11/`](../probe/vbe-shadow-86box-2026-11-11/).
The guest went back to its `ati` files afterwards (boot 155).

## Not known

- An AMD machine: the R3E and HD 8570 read the frame buffer at 5 to 6
  MB/s, so they should gain the most, and they write at 2.5 to 3 GB/s,
  so the second copy costs them more than it costs the 4570.
- An application writing through a Lock of the primary in a window. No
  tool run here did that; DBDD locks in exclusive mode.
- Windowed Direct3D and OpenGL presents, the issue 25 GLQuake log, and
  overlap in both directions (Phase 0, still owed).
- 8, 24 and 32 bpp; only 16 bpp was run. Windows 95.
- A slow-write machine. The Pentium III's 240 MB/s WC writes could make
  the second copy cost more than the reads save on text-heavy work.

## Gates

`build-host.ps1`: the tests for each `Output` bound were seen to fail
before the bound was written. The first file's tests were written after
its code, then seen to catch a deliberately broken inclusive edge.
`run-checks.ps1` passed on the first build and on the final tree.
