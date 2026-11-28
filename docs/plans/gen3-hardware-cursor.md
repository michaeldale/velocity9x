# The Gen3 hardware cursor

Date: 2026-11-24. Status: built, host-tested and run on the GMA 950
netbook (Measured, below). The cursor is configured with GDI acceleration,
again after the mini-VDD maps the engine at Enable; before that it was on
only after a mode change
(`docs/issues/2026-11-24-gen3-gdi-and-cursor-off-until-a-mode-change.md`).
Follows the GDI copies (`gen3-gdi-blitter.md`).

The DIB Engine draws the cursor into the frame buffer. Every GDI call that
touches the screen lifts it off and puts it back (`deBeginAccess` and
`deEndAccess` with `CURSOREXCLUDE`), and a cursor over a window being
redrawn flickers. Gen3's display engine overlays a cursor image on each
pipe that nothing GDI draws touches.

## Decisions

1. **64x64 ARGB, as Linux.** `MCURSOR_MODE_64_ARGB_AX` (0x27), cursor A on
   pipe A and B on B, `CURxCNTR`, `CURxPOS`, `CURxBASE` in that order, the
   BASE write arming the update (i915 `intel_cursor.c`,
   `i9xx_cursor_update_arm`). A move writes POS then BASE. Pipe gamma off,
   so an 8-bpp palette does not recolour the image. The pipes come from the
   enabled primary planes' pipe-select bits.
2. **Only shapes it shows exactly.** A monochrome shape up to 64x64 with no
   inverting pixel (AND 1, XOR 1) goes to the hardware: AND 1 XOR 0
   transparent, AND 0 XOR 0 black, AND 0 XOR 1 white. Anything else -
   Windows' text I-beam inverts, a colour cursor - goes to the DIB Engine's
   software cursor as before, and the overlay is turned off. Every position
   goes to the DIB Engine too, so either can take the next shape.
3. **The image is the reserve's last 16 KiB** (`V9X_I9XX_CURSOR_RESERVE_OFFSET`),
   where the sandbox layout left 0x4000 bytes unused above the depth
   buffer's guard page. The layout's bound now ends below it.
4. **Physical base on 915GM and 945GM.** i915 sets `cursor_needs_physical`
   for 915G/GM and 945G/GM: CURBASE takes a physical address. It is read
   from the image's four GTT entries, which must be present, contiguous and
   below 4 GiB, as the HAL finds the status page's. Pineview and G33 take
   the graphics address.
5. **Off when the screen is not this driver's**: at Disable and when a
   full-screen DOS box takes it (`sswitch16.c`). Put back, image rewritten,
   after the mode is set again (`ResetHiResMode`, `ReEnable`), because a
   VBE mode set may clear video memory.
6. **Where:** `src/chipsets/intel/i9xx_cursor.c`, pure C with host tests,
   and `src/display16/cursor16.c`, both in LEAFCODE. In the intel-gma
   family `dib_thunks.asm`'s SetCursor and MoveCursor jump to
   `V9xCursorSet` and `V9xCursorMove` there; every other family keeps its
   forwards.
7. **Off switch:** `[Velocity9x] HwCursor=0` in SYSTEM.INI. The outcome is
   `HwCursor=` in `C:\V9XDIAG\V9XHW.INI`: `on base=... pipes=...`, or why
   not (`off-ini`, `no-engine`, `no-reserve`, `gtt-unread`,
   `base-refused`, `no-selector`, `no-planes`).

## Not known

- Whether the 945's display FIFO watermarks, as the VBIOS leaves them,
  feed a cursor without underruns. i915 programs no cursor watermark
  outside self-refresh on these parts.
- Whether the DIB Engine's `MoveCursor` with no cursor set does anything
  but record the position. The design relies on that.
- Whether a mode the VBIOS centres on the panel puts the plane at a
  non-zero `DSPxPOS`; the cursor position assumes the plane is at the
  pipe's origin.

## Measure

On the GMA 950 netbook: `HwCursor=on` in V9XHW.INI; the arrow, hourglass
and resize cursors drawn right with the hot spot where it was; the I-beam
still shown (in software); no cursor left over a full-screen DOS box and
the cursor back after it; no change with `HwCursor=0`.

## Measured

The GMA 950 netbook, with the configure-order fix, 2026-11-24: `HwCursor=on
base=3FFAC000 pipes=2` (cursor B, the panel's pipe, a physical base). The
owner's report after using it: "I think its fine" - by eye, with no
detail on the individual shapes, the I-beam or the DOS-box round trip.
The three unknowns above are not settled by that.
