# GeForce 9500 GT (G96): page flips through the VGA CRTC, as on the GT218

Date: 2026-11-28

Phase 3's flips, [docs/plans/nvidia-g96-geforce9500gt.md](../plans/nvidia-g96-geforce9500gt.md),
on M-Z2T02D. Follows [2026-11-28-g96-2d-engine-first-run.md](2026-11-28-g96-2d-engine-first-run.md).

## The VBIOS

`V9XN5MM /rom` copied the shadow at C0000h on boot 10 (signature AA55h,
6Fh x 512 bytes; "Version 62.94.2C.00.91", "G96 Board - 09770001").
Disassembled with capstone beside the GT218's dump
(`build/driver-results/w9qi0dad-gt218-startmove/vbios-c0000.bin`), the
ROM the [GT218 flips record](2026-11-16-gt218-flips-through-vga-crtc.md)
read:

- **Start-address writer, C000:6D8B**: the GT218's C000:5326 instruction
  for instruction. CR0C and CR0D from AX, CR35 from BL, CR34 from BH
  masked to 3Fh. Only the CRTC-port helper's call target differs.
- **4F07, BL=02h, C000:94B2**: the GT218's C000:6ACE path. ECX's low two
  bits to the pan helper, ECX shifted right 2, AX and DX split from it,
  call 6D8B, then CR11 bit 4 cleared.
- **int 10h entry, C000:1C8F**: saves CR3F, writes 57h, as the GT218's
  C000:1873 does; a restore path writes the saved value back.

So the driver's start-address split (`nv50_crtc.c`) and CR3F handling
(`eng_nv50.c`, `v9x_nv50_set_display_start`) apply unchanged. Not
measured with `/startmove` or `/starthigh`: the picture cannot be seen
from here (no capture on this machine, and the agent's screenshot reads
GDI's surface, not the scanout).

## The change

`v9x_g96_device` stamps `FLIP` beside fill and copy (`gt218_hw16.c`, the
manifest's `EngineCaps`). Gates: `run-checks` passed.

## On the card (boot 11)

DRV, VxD, HAL and SETP from the same build staged at `C:\V9XN1-4.BIN`,
renamed in by `WININIT.INI`, `reset`. Staged files and `WININIT.INI`
gone; `V9XDISP.DRV`'s SHA-256 matches the package. `V9XHW.INI`:
`EngineCaps=00000007`, `enable-ok`.

V9XDDP: `Result=COMPLETE`, the fill, copy and overlap pixel keys 1 as on
boot 10, and:

| Key | Boot 10 (no FLIP) | Boot 11 (FLIP) |
|---|---|---|
| `Flip20Ms` | 1 | **349** (17.5 ms a flip: paced by the retrace) |
| `FlipMaxMs` | 1 | 33 |
| `FlipPixelOk` | 0 | 0 |

Snapshot (`V9XSNA1.INI`, same boot): `FlipHandled=23`,
`FlipDeclined=0`, `FlipForcedIdle=0`, `FlipStillDrawing=12882` (polls
answered "still drawing" while a flip waited), `EngineIdleTimeouts=0`,
`EngineFifoTimeouts=0`, `CountFlipToGDISurface=1`.

`FlipPixelOk` was 0 on the GT218 with working flips too; it is not this
change's evidence. What is: flips now wait for the retrace instead of
returning at once, and none were declined.

## Not verified

- That the scanout moves to the flipped buffer: no picture was seen. The
  owner's eyes on a full-screen DirectDraw program, or a capture, would
  settle it.
- 16 and 32 bpp, and any game.
