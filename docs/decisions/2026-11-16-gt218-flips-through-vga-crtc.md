# GT218 page flips through the VGA CRTC emulation, not EVO

Date: 2026-11-16

Machine: W9QI0DAD (10.0.1.172), ASUS GeForce 210, GT218 10DE:0A65, boot
43, desktop 1024x768x16 on origin/main's nvidia set (4439b81a). Probe:
`V9XN5MM.EXE` built from this branch (`d42d9fb7-dirty`). The picture was
read from the card's HDMI output through the OBS capture (screenshots
`claude-live0` to `claude-live4.png`, kept in
`build/driver-results/w9qi0dad-gt218-startmove/`).

## Why

Every Flip on the GT218 row was declined (`FlipDeclined` 68,206 in one
3DMark 2000 run, [2026-11-14-gt218-3d-d3d-builders.md](
2026-11-14-gt218-3d-d3d-builders.md)), and 3DMark 2001 SE's Car Chase
showed cleared and half-drawn frames on screen in 246 of 295 captured
frames ([issue](../issues/2026-11-16-gt218-3dmark2001se-car-chase-cleared-frames-on-screen.md)).
[nvidia-gt218.md](../plans/nvidia-gt218.md) planned flips as Phase 5,
"a method on an EVO channel". nouveau's `nv50_disp_init` takes the
display from the VBIOS when 610024h bit 8 is set; the 2026-11-11 register
probe read 610024h = 10Ch, and head 0's EVO surface-offset mirror reads 0.

## What was measured

1. **`/startmove`**: CR0C/CR0D written 2000h, held 15 s, restored. The
   desktop moved up 16 rows with unwritten memory below it, and came back
   on restore. 2000h x 4 bytes = 32 KiB = 16 rows at a 2048-byte pitch:
   **the head follows the VGA start address, in dwords.**
2. **`/starthigh`, RIVA layout**: CR1F = 57h, then CR19 bit 0. CR1F, CR19
   and every extended index read 0 before and after; the picture did not
   move. **Dead: RIVA/NV4's CR19 and its lock CR1F do nothing here.**
3. **`/vga`**: PDISPLAY 619E00h-619FFFh and 6194E8h read (6194E8h =
   803h; 619F04h = 003FFE09h, a VGA workspace near the top of the
   address space, not the desktop at 0). **Dead: 619F04h as a scanout
   base.**
4. **`/rom`**: the VBIOS shadow at C0000h (signature AA55h, 73h x 512
   bytes). Its VBE 4F07 handler (C000:6A90, table at C000:40C9) takes
   BL=02h's linear address in ECX, shifts it right 2 and calls C000:5326,
   which writes **CR0C = bits 15:8, CR0D = bits 7:0, CR35 = bits 23:16,
   CR34 = bits 29:24 & 3Fh** of the dword address. The low two bits go to
   attribute index 33h (pan). The int 10h entry (C000:1873) saves CR3F
   and writes **CR3F = 57h**, and writes the old value back on exit.
5. **`/starthigh`, VBIOS layout**: CR3F read 0, 3 once 57h was written.
   CR0C/CR0D = 0, CR35 = 01h: the desktop moved up 128 rows (256 KiB),
   with old offscreen memory below. CR35 and CR3F read back 0 after the
   restore, and the picture returned.
6. The VGA status port's retrace bit (3DAh bit 3) rose 30 and 29 times
   in 500 ms on the two runs: 60 Hz, usable by the flip state machine as
   it is for the other VGA-timed families.

## Decision

Flip on the GT218 by writing the start address through the VGA CRTC
emulation, in the VBIOS's layout, with CR3F opened and restored
(`nv50_crtc.h`, `v9x_nv50_set_display_start` in `eng_nv50.c`, FLIP in the
row's caps). No EVO channel. Phase 5 of the plan is answered for the
modes the VBE sets; an EVO channel stays the route only for modes the VBE
does not offer.

## The driver on the card (boot 44)

The branch's nvidia set (`0.54.0 d42d9fb7-dirty`, DRV/VXD/HAL/SETP by
WININIT rename, all four SHA-256 equal to the package; staged files gone),
`reset`, then one 3DMark 2001 SE default run (1024x768x32, Z16,
double-buffered, software vertex processing), driven by agent:

- Snapshot after (`build/driver-results/3dm01-gt218-flips-boot44/`):
  `FlipHandled` 9,336, `FlipDeclined` 0, `FlipStillDrawing` 0,
  `FlipForcedIdle` 0, `BltFlipWaited` 9,231 with 0 timeouts (3DMark's
  clears wait for the pending flip), `DrawsFlipWaited` 0 (the nv50
  Direct3D path has no draw-side flip wait; its draws follow a clear that
  waited), `EngineIdleTimeouts` 0, no NV50 fault record.
- 16 OBS samples of the HDMI output, 8 in Car Chase Low Detail and 8 in
  High Detail: all complete frames with the HUD
  (`build/driver-results/w9qi0dad-gt218-flips-boot44/cc-sheet1.png`,
  `cc-sheet2.png`). Before, 49 of 295 recorded frames were complete.
  Samples, not a recording: a rare torn frame is not excluded.
- Score 2284, recorded, not compared. The desktop came back at offset 0
  behind the score window.

Not measured: whether the emulation latches the start at the retrace or
applies it at once (a recording would show tearing), 16 bpp flips, the
OpenGL ICD's presents, and the 3DMark 2000 helicopter and Quake 3
issues filed the same day.
