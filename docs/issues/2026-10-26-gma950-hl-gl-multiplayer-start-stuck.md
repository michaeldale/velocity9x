# Half-Life OpenGL on the GMA 950: starting a multiplayer game leaves the screen stuck or corrupt until Escape

Date: 2026-10-26. Status: open, not blocking 0.30.0 (Michael). Reported by
Michael on the netbook (W9QI2BE2, GMA 950), 0.30.0 candidate build
`cdbf4cb6`. Not reproduced or measured by a session yet.

## What is seen (Michael)

- Half-Life with the OpenGL renderer: starting a multiplayer game leaves
  the screen stuck, or showing graphical corruption.
- Pressing Escape to the menu and resuming the game makes it draw
  correctly from then on.

## Why it looks like a known fault

It resembles the Millennium II's corrupt Multiplayer menu,
[2026-10-19-mga2164w-hl-gl-multiplayer-menu-corrupt.md](2026-10-19-mga2164w-hl-gl-multiplayer-menu-corrupt.md),
and the netbook's own once-seen garbled menu of
[2026-10-06-netbook-hl-gl-garbled-menu.md](2026-10-06-netbook-hl-gl-garbled-menu.md).
On the Millennium II the menu took the card's textures away with no mode
change the render interface reported: every texture was invalidated after
it, and the ICD now remakes them when it finds them lost. A menu-to-game
transition that loses the textures again, or leaves the back buffer or the
present target stale until a second transition (Escape and resume), would
fit both pictures. That is a reading, not a finding.

## What is not known

- Whether it is new in 0.30.0 or older. Nothing in 0.30.0 changed the
  ICD. The GMA 950 gained write-combining in this release, and the
  drain is before every ring submission and plane-base flip; a missing
  drain would look like stale or partial frames. It can be told apart
  by running the same steps on 0.29.0, or with `C:\V9XDIAG\NOWC.TXT`
  present.
- Whether Direct3D does the same: on the Millennium II it did not.

## Next

1. Reproduce on the netbook: the steps to the multiplayer start, an
   agent screenshot at the stuck frame, then `C:\V9XDIAG\V9XGL.LOG` and a
   V9XTRACE snapshot for lost textures (`counters hwtex lost=`), refused
   draws and presents.
2. The same with `NOWC.TXT` (write-combining off) and on 0.29.0.
