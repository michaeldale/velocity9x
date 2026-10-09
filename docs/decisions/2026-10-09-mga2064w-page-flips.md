# MGA-2064W: page flips

Date: 2026-10-09

The first item under Later in [the Matrox plan](../plans/matrox-millennium-family.md).
The matrox family now stamps `V9X_DD_ENGINE_CAP_FLIP`, and the HAL moves
the display start for a DirectDraw flip instead of declining it.

## The start address, and the disagreement about its units

The start is a 20-bit value: CRTC0D (bits 7:0), CRTC0C (15:8) and
CRTCEXT0<3:0> (19:16). The sources disagree about what it counts:

- **MGA-1064SG specification, section 5.6.5 (p.5-66).** This chip has the
  2064W's 2D core. It gives startadd in pixels divided by 8, 4 and 2 at 8,
  16 and 32 bpp, which is 8 bytes per unit, and the CRTC13 offset as
  `pitch * bpp / 128`, which is 16 bytes per unit.
- **FreeBE's Matrox driver.** For device 0519 only, the 2064W, it divides the
  byte address by 4 and the line length by 8. For the Mystique and the
  Millennium II it divides by 8 and 16.

The card settles which applies to it. A8U4I5's BIOS at 1024x768x16 left
CRTC13 = 80h with CRTCEXT0's offset bits 0. That is 128 units for a
2048-byte line, 16 bytes per unit: the 1064SG's rule, not FreeBE's
([V9XTIME capture](../probe/a8u4i5-mga2064w-timing-2026-10-09/V9XTIME-velocity9x-1024x768x16.INI)).

In VGA byte addressing a start unit is half an offset unit under both
rules. So the HAL derives the start unit from the live CRTC13 and pitch at
every flip, and refuses any ratio other than the two known ones.
`v9x_mga_display_start` (host-tested, in `mga_engine.c`) does that
arithmetic. It also refuses:

- a start that is not a whole number of units,
- a start that does not fit 20 bits,
- an interlaced mode, whose addressing nothing here describes.

## When the write lands

The 1064SG says a new start takes effect "at the beginning of the next
horizontal retrace following the write to CRTCEXT0". CRTCEXT0 is written
last, as it asks. That means the change applies at the next line and is
not latched at the retrace. So the MGA is a "writes in blank" scanout, like
the Rage IIC:

- the core writes the start inside the VGA vertical blank,
- it completes the flip when that blank ends,
- it answers WASSTILLDRAWING outside the window.

A write near the end of the blank could tear the top lines. There is no
guard against that here, as the Mach64 has one, and none was measured.

## Measured

- **86Box Win98SE-Millennium guest (boot 49):** `V9XDDP` COMPLETE,
  `FlipHr` OK, 20 flips in 332 ms. With `/hold`, a host-side PrintWindow
  capture of the emulated scanout showed the whole screen red during the
  first hold and blue during the second
  ([frames](../probe/a8u4i5-mga2064w-flip-2026-10-09/)). 86Box's model of
  the 2064W is not evidence about the silicon's units.
- **A8U4I5, physical 2064W, 1024x768x16:**
  - `V9XDDP` COMPLETE with the fill and all four overlap cells correct,
    `FlipHr` OK, 20 flips in 331 ms (at most 17 ms each).
  - Before this change the same probe on the same card reported
    `Flip20Ms=0`: the flips were declined and DirectDraw copied instead.
    Now they are paced by the retrace, so the HAL is handling them and
    waiting for the blank.
- `FlipPixelOk=0` on both machines. That check reads the screen through
  GDI, which once real flips happen sees only GDI's page. The settings
  page already leaves it out for that reason.

## Not established

- **What the card scans out.** The agent's screenshot reads the GDI
  primary, and there is no host capture of a physical card. The units rest
  on the BIOS's CRTC13 and the VGA ratio until someone watches `V9XDDP
  /hold` on the monitor: a whole red screen for five seconds, then a
  whole blue one. A wrong unit would show half the desktop or another
  surface.
- Tearing at the top of the frame, as above.
- 8 and 32 bpp, and the Millennium II.
