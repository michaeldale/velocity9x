# Glide's command FIFO sits inside our desktop, and GDI's CPU drawing jams it

Date: 2026-10-12. Machine: A8U4I5, boots 423 and 424. Card: Voodoo4 4500 PCI
(subsystem 0005121A). Driver: `vsa100-tier0` at `8a3529e` (Phase 4 step 3).
Game: Diablo II shareware 1.0 with `-3dfx` and 3dfx's `GLIDE3X.DLL`
3.10.00.0658 in its folder. Evidence:
[step4-boot424-diablo2-exit-hang](../probe/a8u4i5-voodoo4-glide-2026-10-11/step4-boot424-diablo2-exit-hang/),
[step4-boot423-diablo2](../probe/a8u4i5-voodoo4-glide-2026-10-11/step4-boot423-diablo2/).

## Symptom

Diablo II on 3dfx's Glide draws the title, the menus and the town
correctly (Michael, at the monitor, boot 424). On "Exit Diablo II" the
picture froze on the main menu with wrong colours, Glide never sent
`RLSEXCLUSIVE`, and the machine needed a reset. At boot 423 the machine
crashed the same way, after `V9XGDI /auto` had drawn its window
mid-game.

## Measured, with the game hung in its exit (boot 424)

`status` read `0x00002B9F` on two reads several seconds apart: busy (bit 9),
with the 3D engine, TREX, CMDFIFO 0 and TMU 0 busy. Command FIFO 0, read
with `VSA100.EXE /notile /engine`:

| Register | Value | Reading |
|---|---|---|
| `cmdBaseAddr0` (80020h) | `0x18` | the FIFO starts at 18000h |
| `cmdBaseSize0` (80024h) | `0x1FE` | enabled, 255 pages: 18000h-117000h |
| `cmdRdPtrL0` (8002Ch) | `0x8C554` | the read pointer, stopped |
| `cmdAMin0` / `cmdAMax0` | `0x1800C` / `0x9C878` | |
| `cmdFifoDepth0` (80044h) | `0x6188` | 24,968 words not executed |
| `cmdHoleCnt0` (80048h) | `0xD616` | 54,806 unwritten words between AMin and AMax |

Video: overlay on, desktop off, 800x600. Glide was still scanning out its
own buffer.

Our desktop surface at 800x600x16 is 0h-EA600h, from the VBE linear base at
offset 0. It lies almost entirely inside Glide's FIFO. The first title-screen
screenshot (boot 423) shows the same layout: the top 60 lines, about 18000h
bytes, black, and Glide's tiled data from there down.

## Cause

On this chip a CPU write to a framebuffer address inside an enabled
CMDFIFO's range is taken as a FIFO write. That is how Glide fills its FIFO
(vsa100-registers.md, CMDFIFO). Step 3 kept GDI off the 2D engine while
Glide held the card, but GDI still draws through the DIB Engine with the
CPU, into the desktop at offset 0, which is inside Glide's FIFO. Each such
write puts stray words into the FIFO and moves AMax past slots Glide never
fills. The non-zero hole count is the FIFO waiting for them, so the 3D
engine stalls. Glide's `grSstWinClose` waits for idle before it releases,
so the game hangs in its exit with its last, gamma-faded frame on screen.

What drew at the two hangs:
- boot 423: `V9XGDI /auto`, run mid-game;
- boot 424: Windows repainting the desktop and taskbar as Diablo II began
  to quit, while Glide was still running its fade.

## How 3dfx's driver avoids it

It puts the desktop at the top of memory (`vidDesktopStartAddr = 01F60000`
at 640x480x16;
[2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-top-of-memory.md](../decisions/2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-top-of-memory.md)).
Glide's FIFO and buffers then sit below it, and GDI drawing to the hidden
desktop during a session lands nowhere Glide uses. What `fbRam` 3dfx's
driver reports was not captured.

## Not established

- Which exact writes jammed it. The overlap and the hole count fit; the
  writes were not traced.
- Whether the AGP card behaved the same. Its sessions in steps 2 and 3
  closed cleanly, but those sessions were short and nothing repainted
  during them.

## Status

Fixed by moving the desktop above Glide's memory, as 3dfx's driver does
([2026-10-12-voodoo4-desktop-above-glide.md](../decisions/2026-10-12-voodoo4-desktop-above-glide.md)).
On boot 427 Diablo II's exit returned to the desktop with the FIFO empty.
