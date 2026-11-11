# The vbe screen shadow on an AMD machine, at 8 and 32 bpp, and the open items: R3E reads 75-250x faster, scrolling up is no longer 7x slower than down

Date: 2026-11-11

Status: measurements and checks on the committed build `2dfb21ec`. Follows
[`2026-11-11-vbe-screen-shadow-phase1.md`](2026-11-11-vbe-screen-shadow-phase1.md),
whose "Not known" list this works through. No code changed.

Evidence: [`../probe/vbe-shadow-open-items-2026-11-11/`](../probe/vbe-shadow-open-items-2026-11-11/)
(every dispbench run, the per-depth tables `bench\*-off-vs-on.md`, the scroll
results, the 86Box captures, the two scratch tools' sources).

## An AMD machine: M-JJN1ZZ (GX-412HC, Radeon R3E)

The `vbe` DRV/VXD/HAL swapped over its `ati` set by WININIT (boots 29-31),
write-combined through an MTRR (`MtrrWrite=ok`), then put back (boot 32,
hashes checked). Remote help on. dispbench DBGDI at 1024x768, MB/s:

| Operation, 256x256 | 16 bpp off / on | 8 bpp off / on | 32 bpp off / on |
|---|---:|---:|---:|
| Screen to screen | 4.7 / 384.9 | 4.7 / 354.7 | 4.7 / 399.6 |
| Overlapping copy | 4.7 / 790.7 | 4.7 / 619.8 | 4.7 / 864.4 |
| Screen to bitmap | 5.3 / 807.7 | 5.3 / 682.0 | 5.3 / 943.5 |
| ScrollDC | 4.7 / 1,182.3 | 4.7 / 1,172.3 | 4.7 / 763.3 |
| FillRect | 2,351 / 859 | 2,201 / 697 | 2,385 / 998 |
| Ellipse 128x128 | 214 / 126 | 101 / 73 | 376 / 251 |

Reads are 75 to 250 times faster. Writes run at 0.32 to 0.73 of their old
speed, more of a cost than on the 4570 (0.44 to 0.81), as expected from
writes at about 2.5 GB/s. Text costs 1.3 to 1.9 times as much; a
horizontal line 1.35 times; a 30-degree line 2.4 to 3.4 times (its bounding
box is copied, and here the copy is dearer). The 8 bpp shadow-off DBGDI
died after 126 of its 131 sections; the rows above are among the 126, and
it was not rerun.

## The i5-4570 at 8, 16 and 32 bpp, committed build

Boots 207-209, then back on its boot-200 intel-gma files (boot 210).
Screen to screen 256x256: 103 to 2,112 MB/s at 16 bpp, 25 to 1,537 at 8,
134 to 2,752 at 32. Overlapping copy: 23.8 to 2,664, 24.2 to 3,790, 23.9 to
3,687. Writes 0.44 to 0.81. The final build's numbers at 16 bpp match the
earlier record's within 3 %, so the device-bitmap `Output` change cost
nothing measurable there.

## Scrolling in both directions (Phase 0, owed)

A scratch tool (`scrollt.c`) times 20 scrolls of an 800x600 window by 16
pixels in each direction, by overlapping BitBlt and by ScrollDC.
Microseconds per call:

| Machine | Shadow | Up | Down | Left | Right |
|---|---|---:|---:|---:|---:|
| i5-4570 | off | 11,279 | 79,085 | 11,354 | 79,101 |
| i5-4570 | on | 174 | 225 | 155 | 217 |
| R3E | off | 394,789 | 401,839 | 401,600 | 403,824 |
| R3E | on | 2,658 | 2,109 | 2,685 | 2,133 |

("Up" moves the content up, which is scrolling down a document.) The
4570 without the shadow has issue 15's asymmetry: content moving down,
which is scrolling up a document, is 7 times slower. A copy whose
destination is below its source runs backwards and reads the write-combined
frame buffer in the slow order. With the shadow the asymmetry is 1.4 times
and every direction is 65 to 360 times faster. The R3E reads so slowly that
direction makes no difference to it. The W9000 of issue 15 was not run.

## Correctness, 86Box (Win98SE-Mach64VT2), committed build

Frame buffer (emulator capture) against shadow (GDI capture), boot 156:

- **A windowed program writing through a DirectDraw Lock** (scratch
  `lockw.c`): one Lock with a rectangle, one without, both writes then
  Unlock, in normal cooperative mode. Both patterns are in the shadow;
  without the HAL's read-back at Unlock that area would hold the window's
  white background. Only the cursor and the agent's icon differ.
- **Windowed OpenGL** (V9XGLP, software renderer). Three of five
  capture pairs taken while it drew matched; the other two show different
  GL scenes in the two captures, because the probe changes scene faster
  than the two captures are taken. The shadow did hold rendered GL frames,
  which only the HAL's blit into it can have put there. V9XGLP's results
  match a shadow-off run of 0.37.0 key for key where compared
  (`LitResult=differ` was already so). Windowed Direct3D was not run
  separately; it presents through the same HAL blit.
- **8 bpp**: shapes, the Lock patterns and the desktop. Outside the two
  Lock areas only the cursor and the icon differ. Inside them the captures
  differ in colour only: the tool writes arbitrary palette indices, which
  the emulator shows through the hardware palette and a GDI capture through
  GDI's colour table. The same indices are in both.
- **32 bpp**: the Mach64 VT2 guest refuses 1024x768x32
  (`ChangeResult=-2`), so 32 bpp has numbers (above) and no frame-buffer
  comparison.

The guest went back to its `ati` set afterwards (boot 157).

## The issue 25 GLQuake present

Not the shadow's: the software GL back buffer is in video memory, so a
windowed present reads it out of the frame buffer.
Filed as [`../issues/2026-11-11-vbe-soft-gl-back-buffer-in-video-memory.md`](../issues/2026-11-11-vbe-soft-gl-back-buffer-in-video-memory.md),
with the R3E's present times (126-168 ms off, 63-73 ms on, for a
360x280 window).

## Tools dying

dispbench tools died three times today, with and without the shadow: DBGDI
on the R3E at 8 bpp with the shadow off; DBTIME and DBGDI on the 4570 at 16
bpp with it on, 415 s after boot. A DBTIME death does no drawing. With the
earlier record's one DBGDI death, that is no evidence for or against the
shadow; the cause is not known.

## Still not known

Windows 95. A slow-write machine (the Pentium III's 240 MB/s). A
1.4-times up/down difference that remains on the 4570 with the shadow. The
W9000 itself.
