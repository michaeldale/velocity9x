# GDI acceleration, build 004: what "CPU-to-screen upload" should actually be

Date: 2026-08-27
Status: accepted (design); implementation default-off per the rollout table

[`docs/plans/gdi-acceleration.md`](../plans/gdi-acceleration.md)'s rollout table
lists build 004 as "CPU-to-screen upload (design after 003)", default **off**,
exit gate "same harness with memory-source ops". This is that design, and it
narrows the build: **the colour upload path is not worth implementing, and the
monochrome expansion path is.**

## The two hardware paths

The reference driver has separate commands for them, which is the first clue
that they are not one feature. From `98DDK\src\display\mini\s3v\S3.INC:828`:

```
CLRSRCBLT   equ BITBLT + bSRC_Sys
MONOSRCBLT  equ BITBLT + bSRC_Sys + bSRC_Mono
```

`bSRC_Sys` (0x80) means the CPU supplies the bitmap data rather than the engine
reading it from display memory. `bSRC_Mono` (0x40) additionally means that data
is one bit per pixel, which the engine expands to the destination depth using
the pattern foreground and background colours.

Both feed data the same way: the CPU writes it to the image-transfer window,
which `S3.INC:688` puts at **MMIO offset 0** with a maximum burst of
`IMAGE_XFER_MAXBYTES = 0x8000`. Offset 0 is inside the 64 KiB window this
driver's engine selector already covers, so no new mapping is needed.

The transfer itself is a plain `rep movsd` into `ES:0`, restarted at offset 0
per scanline, with **no FIFO polling in the loop** - the engine throttles the bus
itself and the writes stall until it can accept them (`ColorSourceBlt`,
`S3BLT.ASM:1400-1420`). That is worth knowing before writing a pacing loop that
is not needed.

## Why colour upload is not worth implementing

Count the bytes the CPU moves across the bus for a W x H blit at 8 bpp:

| Path | CPU reads | CPU writes to the bus |
|---|---|---|
| DIB Engine | W x H from system RAM | W x H to the framebuffer |
| `CLRSRCBLT` | W x H from system RAM | W x H to IMAGE_XFER |

Identical. The engine is not reading the source - the CPU is still moving every
byte, just to a different destination on the same bus. The engine's advantages
over the DIB Engine here are hardware ROP application and hardware clipping,
and **neither is in play for SRCCOPY**, which is the operation a memory-to-screen
blit almost always is.

Against that nil benefit, the costs are real and specific to this driver:

- **Segment arithmetic.** The 32-bit reference walks the source with
  `ds:[esi]`. A 16-bit driver has a selector and a 16-bit offset, so a source
  bitmap larger than 64 KiB needs selector stepping - and a 1024x768x8 DIB is
  768 KiB.
- **A fault risk the reference guards against explicitly.** The last scanline's
  final dword may not exist: `CSB_AllLastDword` (`S3BLT.ASM:1425-1460`) reads a
  dword from *before* the end and shifts it down precisely to avoid reading past
  the source. Getting that wrong is a GP fault in a display driver.

So colour upload is fiddly, faultable, and buys nothing measurable. It is not
implemented. If a later build wants it, the reason to revisit is hardware ROP
application for a non-SRCCOPY memory-source blit - not throughput.

## Why monochrome expansion is worth implementing

Same count, for a 1-bpp source:

| Path | CPU writes to the bus | Expansion |
|---|---|---|
| DIB Engine | W x H bytes to the framebuffer | in software, on the CPU |
| `MONOSRCBLT` | W x H / 8 bytes to IMAGE_XFER | in the engine, free |

**Eight times less CPU-to-bus traffic at 8 bpp and sixteen times less at 16 bpp**,
with the expansion moved off the CPU entirely. That is a real win of the same
kind the fill and copy primitives delivered, and it is why the reference driver
carries a distinct command for it.

The workload is icon and mask blits - `BitBlt` from a 1-bpp bitmap, which is
what a monochrome mask or a DDB icon draw is. Text is **not** in this set: glyphs
arrive through `ExtTextOut` and `StrBlt`, different ordinals this driver still
forwards to the DIB Engine, so accelerating text is a separate piece of work and
not part of 004.

## The colours, and a lesson from build 001

Mono expansion needs a foreground and a background colour, and they come from
the `lpDrawMode` argument - the eleventh parameter, which no build so far has
read. `GDIDEFS.INC:1283` lays `DRAWMODE` out as `Rop2` (0), `bkMode` (2),
`bkColor` (4), `TextColor` (8).

Build 001 was caught out by a DDK comment that labelled a field "Physical fg
color" when it held a logical `COLORREF`
([the issue](../issues/2026-08-26-gdi-fill-brush-colour-not-physical.md)). This
struct is more trustworthy on its face, because it carries **both** forms at
separate offsets - `bkColor`/`TextColor` described as physical, and
`LbkColor`/`LTextColor` at 24 and 28 described as logical. A struct that
distinguishes them is far less likely to be confusing them.

Less likely is not the same as verified. The implementation reports both pairs
through `V9X_GDI_STATS` so the first run says which is which, exactly as
`LastBrushBpp` and `LastBrushStyle` did for the fill.

## Scope of the build

- **Implement** `MONOSRCBLT` on the ViRGE and its 8514/A equivalent on the
  Trio64, behind a new `GdiAccelUpload` key, **default off** as the rollout
  table specifies.
- **Do not implement** colour upload. Decline it, count the declines, and let
  the harness confirm they happen.
- **Harness**: memory-source operations of both kinds. The mono ones must
  accelerate when the key is on and decline when it is off; the colour ones must
  decline always. Both claims checked, in the shape builds 002 and 003
  established.
- **BARRY**, the physical Trio64, is where this wants a real-silicon run: the
  8514/A image-transfer path is the one part of this that emulation is least
  likely to model faithfully, since it is a bus-timing behaviour rather than a
  register behaviour. Noted as required and not yet done - that machine is
  currently powered off.
