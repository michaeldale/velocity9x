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
