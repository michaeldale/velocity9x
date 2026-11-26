# A small flickering square follows the mouse over a full-screen game (HD 5570, 0.48.0)

Date: 2026-11-13. Status: open. Field report only, no files; not
reproduced on any of our machines.

Machine: the reporter's HD 5570 (Redwood, `1002:68D9`) on
[GitHub issue 16](https://github.com/michaeldale/velocity9x/issues/16),
released 0.48.0 `ati` package, DirectX 6.1a. Context and earlier runs:
[`../decisions/2026-11-07-redwood-row-unmeasured.md`](../decisions/2026-11-07-redwood-row-unmeasured.md).

## What was reported

In Bug!, at both 320x240 and 640x480, the mouse pointer shows over the
game as a small flickering square instead of a cursor. It moves with the
mouse and can be pushed aside but stays visible. The reporter does not
remember it on earlier versions; the last they ran before 0.48.0 was
0.40.0.

## Leading suspect, not tested

The screen shadow that 0.47.0 turned on for Evergreen
([`../decisions/2026-11-12-ati-screen-shadow.md`](../decisions/2026-11-12-ati-screen-shadow.md)).
On a cursor exclusion (a `deBeginAccess` without `FB_ACCESS`, which
DirectDraw issues around blits and Locks of the primary) the shadow
reads the cursor's rectangle back and copies it out. That record says
the shadow skips exclusive mode, but Bug! at 320x240 runs in one of our
VBE modes (`0193h`), not DirectDraw's Mode X, so whether its frames take
that path is not known. A square the size of the cursor that tracks the
mouse fits a cursor-rectangle copy-out over the game's picture.

Other candidates, equally untested: the 0.45.0 INT 2Fh screen-switch
hook, and the GDI cursor not being hidden when the game takes the
screen.

## Next

- Reporter: create `C:\V9XDIAG\NOSHADOW.TXT`, reboot, run Bug! again.
  Square gone points at the shadow.
- Ours: the shadow is shared with `vbe` and the other `ati` rows, so if
  it is the shadow, a full-screen DirectDraw game that keeps GDI's
  cursor should show it on any of them (Carrizo, R3E, or a `vbe` guest).

## 2026-11-25: reproduced on the Oland, and it is the shadow

A new probe, V9XCURSQ, behaves like a full-screen DirectDraw game that
never hides the cursor. On W9QI0DAD's Oland (`ati`), at 640x480x16 with
a flip chain, it found a 36x40 solid box at the cursor in the buffer at
offset 0: the arrow on the black GDI had drawn there. It came back the
same on a second boot. With `NOSHADOW.TXT` the box is gone, leaving the
arrow over the game and some of the software cursor's own background
blocks. The cause: the shadow copies the cursor's rectangle out of
itself on every cursor move, while in exclusive mode the HAL has stopped
keeping the shadow in step with the screen
([record](../decisions/2026-11-25-1.0-b6-oland-cursor-square.md)).
Status: reproduced, cause found, not fixed.

## 2026-11-25, later: fixed on the Oland

While a process holds exclusive mode, the HAL now says so in
`V9X_DD_SHARED.shadow_hold`. The shadow then reads the cursor's
rectangle back from the frame buffer before the cursor is saved under
or drawn. On boot 136 V9XCURSQ reads the arrow over the game (57 % of
its box, verdict CURSOR, exit 0), where it read a 95 % square before.
The release probes read as before. Not yet confirmed by the reporter or
run on the R3E or R7E
([record](../decisions/2026-11-25-1.0-b6-oland-cursor-square.md)).
Status: fixed, awaiting the reporter.

Later the same day the fix ran on the R3E (M-JJN1ZZ): V9XCURSQ reads
the arrow over the game, as on the fixed Oland, and the release probes
are as at 0.55.0. The old driver was not run there, so the square was
not reproduced on the R3E.

## 1.0 re-read, 2026-11-26

Stays open under 1.0 blocker B6. Fixed on 2026-11-25 and measured with
V9XCURSQ on the Oland and R3E; blocker B6 waits on the reporter.

## 2026-11-26, later: still there at 320x240 on an HD 5450

The HD 5450 (Cedar) in W9QI0DAD, on the 0.61.0 tree with the fix, has
a 320x240x16 mode, which the Oland and R3E do not. V9XCURSQ: 320x240
flip **SQUARE** (box 168,120-204,160, 95 %, the arrow on the stale
black), exit 2; 640x480 flip and blit show the arrow over the game, as
on the fixed Oland. The reporter's Bug! ran at 320x240, so the fix does
not cover their case yet. The 320 case is also the probe's first, so
mode and order are not separated; cause not found
([record](../decisions/2026-11-26-1.0-hd5450-evergreen-check.md)).
Status: open. Left for 1.1 (Michael, 2026-11-26): B6 is closed for
1.0 with 640x480 fixed, and the README names the 320x240 square as a
known issue. First step for 1.1: run V9XCURSQ with 640x480 first, to
separate the mode from the probe's case order.
