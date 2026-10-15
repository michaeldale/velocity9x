# 3DCIF games on a tester's Rage Pro class card: Tomb Raider and Formula 1

Date: 2026-10-16. Status: open. One cause fixed in source (keyed and
masked draws, below); untested on hardware.

Source: a tester's messages, passed on by Michael on 2026-10-15 and
2026-10-16.
Velocity9x 0.18.0 `ati`, DLL build `de564208`. The card is Rage Pro
class: every session describes engine 4 (Mach64). The chip, its PCI id
and the machine's `SYSTEM.INI` were not sent. Evidence:
`docs/probe/field-rage-pro-3dcif-games-2026-10-16/` (`V9XCIF.LOG`, the
Tomb Raider fault text, three screenshots converted from BMP to PNG, and
the two Cubes photos from the first report).

## What the tester reported

- **ATI's Cubes demo** clears blue under Velocity9x and grey under ATI's
  driver (`cubes-velocity9x.jpg`, `cubes-ati-driver.jpg`; the YouTube
  capture of a Rage Pro, and our own capture under ATI's driver on the
  Rage IIC, `a8u4i5-rage-iic-3dcif-ati-2026-10-13/cubes-b.png`, are grey
  too).
- **Tomb Raider (`TOMBATI.EXE`)**: in game the background is black and
  Lara's shape shows with stripes; screenshots of the game come out
  black. The menu's book frame is faintly visible with no text in it.
  On exit: "TOMBATI caused a stack fault in module TOMBATI.EXE at
  0187:00447593" (`tombati-fault.txt`).
- **Formula 1 (`F1ATI2.EXE`, `F1ATI.EXE`)**: in game, untextured white
  and grey geometry (`formula1-race.png`); a menu with bands of repeated
  characters (`formula1-menu.png`).
- Copying ATI's own ATI3DCIF.DLL into the Tomb Raider folder gave an
  "ATi cif error", as expected: ATI's DLL needs ATI's driver.

## What the log shows

`V9XCIF.LOG`, 15,057 lines: two F1 builds, nine Tomb Raider sessions,
two Cubes sessions.

### Keyed and masked draws refused (fixed in source)

Every refusal logged in detail (the first 16 a session) is the same
draw, in both games:

    draw refused result=4 (UNSUPPORTED)
      format=2 (XRGB1555 target) texture=2 (hardware)
      blend=0 src=2 dst=1
      alphatest=1 fog=0 texformat=3 (ARGB1555)
      ops color=2 alpha=2 (MODULATE, MODULATE)

- Tomb Raider's are its chroma key (`C3D_ETEXOP_CHROMAKEY`; no
  "approximately" note). F1's are the alpha mask (`texop=3`,
  `C3D_ETEXOP_ALPHA_MASK`, noted as approximated).
- The DLL drew both as an alpha test on texel alpha times vertex alpha.
  Its fallback to the texel's alpha alone applies only when every
  vertex is opaque; these vertices are not, and the Mach64 refuses the
  product.
- Scale: Tomb Raider sessions refuse 30-65% of their triangles (one:
  344,930 drawn, 623,448 refused). F1's longest session: 2,557,691
  drawn, 3,709,195 refused.

With blending off nothing reads the alpha but the test, so the vertex
alpha changes nothing on ATI's hardware, and the product would also drop
texels wherever a vertex alpha is 0. `cif_state.c` now tests the
texel's alpha alone when blending is off, for both ops; the alpha mask
is then exact rather than approximated. `test_cif_state.c` was changed
first and failed six checks.

Not measured: whether the Mach64 accepts the new draw on this tester's
chip. Pixie's keyed textures, through the opaque-vertex fallback, take
the same alpha op on the Rage XL.

### F1's quadrangle and rectangle strips are skipped

F1's longest session: 1,896,715 `RenderPrimStrip` calls, 1,870,556 of
them skipped as `primitive` (`detail=00030000`: `C3D_EPRIM_RECT`, strip;
`00020000`: `C3D_EPRIM_QUAD`, strip), almost all of 4 vertices. The DLL
draws only quadrangle *lists*. This is probably most of F1's missing
picture.

The SDK guide (`ragesdk.pdf`, SDK-C02700 Rev 1.30, chapter 2 and pages
5-11/5-12) names both types "list or strip" and gives no vertex order
for either. glrage has a TODO for quad strips and treats rectangles as
triangles. The order is to be measured under ATI's driver, not guessed:
a scene drawing one 4-vertex strip of each type with distinct vertex
colours, read back.

### The display layout disagrees with what is drawn

- **Cubes** clears with `8410` (`pixel before draw: 8410` on every
  frame), the 5:6:5 grey. Into a 5:5:5 scan-out that is (8,0,132), the
  blue in the photo. Under the Rage IIC's 5:5:5 on A8U4I5 the same demo
  cleared `4210`, the 5:5:5 grey (boot 463). So Cubes takes the layout
  from DirectDraw, and on this machine DirectDraw described 5:6:5 while
  the screen showed 5:5:5.
- **Tomb Raider**: every session logs `device: the display is 5:6:5`
  after its mode change, from the DLL's own `GetDisplayMode`, while the
  render interface describes an XRGB1555 target (`target
  formats=00000004`) and the draws go out as `format=2`. The game's own
  2D (menu text, the book) would then be written as 5:6:5 into a
  surface drawn and shown as 5:5:5.

The mode table publishes 5:5:5 masks only for a 16 bpp row whose VBE
mode has a 15 bpp sibling, and the mode set programs the sibling by the
same rule (`modes16.c`, `v9x_modes16_row_is_555`). How the two came
apart here is not known. Candidates, none tested:
- the tester's BIOS lacks the 15 bpp sibling of the game's mode
  (640x480), so the row stays 5:6:5 while something else programs
  5:5:5;
- the render interface's description is not refreshed after a
  DirectDraw mode change;
- a stored `HighColor` setting.

Needed from the tester: `V9XHW.INI`, `V9XDD.INI`, `V9XBOOT.INI`, the
`[Velocity9x]` section of `SYSTEM.INI` and the card's name. On our side,
A8U4I5's Rage XL running Cubes and reading the clear value would show
whether this reproduces; A8U4I5 was off on 2026-10-16.

### Tomb Raider's stack fault on exit

Not examined. `ESP=008C2000` sits on a page boundary, which fits stack
exhaustion. The last DLL calls before it were not isolated.

## Next

1. The tester runs a build with the keyed-draw fix: Tomb Raider and F1
   refusal counts should fall to near zero.
2. The files above, for the layout.
3. Measure quadrangle and rectangle strip order under ATI's driver.

## Formula 1 on A8U4I5 with the fix (2026-10-16)

The tester's source (archive.org `formula-1_202505`, OEM ATI Rage Pro
disc) installed through its Wise installer to `C:\Program Files\Formula1`,
Psygnosis's Rage Pro patch beside it as `F1ATI2.EXE`, the disc's data
track mounted on D: with DAEMON Tools (the game refuses to start without
it: "Formula 1 CD NOT Found"). Rage XL, boot 482, 5:5:5 desktop, the DLL
from `7640e11c` in the game folder. Evidence:
`docs/probe/a8u4i5-rage-xl-formula1-2026-10-16/`.

- The Silverstone demo race draws textured: cars, track, barriers,
  grandstands and sky (`silverstone-demo-race.png`; the tester's 0.18.0
  capture of the same race was untextured white and grey).
- 1,589,684 triangles drawn, 52,143 refused (3%, against the tester's
  59%). Every remaining refusal logged is a blended draw under the alpha
  mask (`blend=1 src=1 dst=4`, one `src=5 dst=2`): texel alpha times
  vertex alpha with blending on, the gap filed for Pixie's sprites
  ([issue](2026-10-14-3dcif-blended-sprites-refused.md)).
- The quadrangle and rectangle strips (187,896 skipped) come almost all
  from the menus and loading screens: the race added about 1,000. The
  loading screen drew correctly in the agent's capture.
- The 640x480 race ran at 5:5:5 here: no "display is 5:6:5" line.

Assault Rigs, run the same day, found a second cause of white geometry:
a 68-byte `C3D_TMAP` the DLL refused
([record](../decisions/2026-10-16-3dcif-early-tmap-68-bytes.md)). F1
registers the 88-byte Rage PRO form and was not affected. Assault Rigs' 400x300 menu mode
has no 15 bpp sibling and runs 5:6:5, which is one way for a title to
meet a 5:6:5 display on an ATI card set to 5:5:5.
