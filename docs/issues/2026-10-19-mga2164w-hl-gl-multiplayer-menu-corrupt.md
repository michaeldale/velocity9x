# Half-Life OpenGL's Multiplayer menu is corrupt on the Millennium II, every time

Date: 2026-10-19. Status: open, parked at Michael's word ("record as
issue and move on" unless the fix is simple; it is not). Seen on
A8U4I5 (MGA-2164W, 8 MiB) at boots 499, 501 and the 2026-10-19 builds
(`69abf8bc-dirty`, `8ae2370e-dirty`). Evidence:
`docs/probe/a8u4i5-mga2164w-render-interface-2026-10-18/`, and the
record [2026-10-18-mga2164w-half-life-gl-split.md](../decisions/2026-10-18-mga2164w-half-life-gl-split.md)
(boot 499 and 2026-10-19 sections).

## What is seen

Half-Life `-gl` at 640x480x16, the WON Multiplayer menu (Michael's OBS
captures, 2026-10-18 and 2026-10-19):
- the menu art smeared into horizontal streaks;
- each description line drawn twice, a few rows apart;
- some rows of coloured noise.

The game itself draws correctly after the menu, and the title
("Multiplayer") is clean.

This is the picture of the netbook's once-seen fault,
[2026-10-06-netbook-hl-gl-garbled-menu.md](2026-10-06-netbook-hl-gl-garbled-menu.md):
noise tiles, description lines repeated and shifted, streaks of texture
colour. There it could not be reproduced; here it happens every time,
which makes the 2164W the machine to chase it on.

## What is known

- **OpenGL only.** Under Half-Life's Direct3D renderer on the same build
  the menu is clean (Michael, 2026-10-19).
- **The menu takes the card's textures away.** On boot 500 every card
  texture was invalidated after the menu (`DDRAWISURF_INVALID`,
  `DDRAWISURFGBL_MEMFREE`, `fpVidMem` 0) with no mode change the render
  interface reported. Since a6da93e6 the ICD remakes them
  (`counters hwtex lost=30` on 2026-10-19) and the game draws correctly
  afterwards, but the menu is still corrupt.
- **Most of the menu is drawn from the CPU.** Its pictures are non-square
  textures the ICD keeps on the CPU (`cpu-nonsquare` 194 to 246 batches a
  session), drawn by the software fallback into the card's back buffer.
  After bf287927 some of its 4444 pictures may now go to the engine
  instead; the corruption was still there on that build.

## Ruled out

- **GDI at 640x480x16.** The desktop set to that mode
  (`V9XMSW /set:640x480x16`) draws Notepad and the desktop correctly.
- **Engine state left for GDI.** Every MGA GDI operation writes MACCESS,
  the plane mask and the clip window first (`v9x_gdi_mga_draw`), so the
  nodither bit an explicit draw sets cannot reach it.
- **A save-under in VRAM.** The 16-bit driver has no SaveScreenBitmap;
  nothing of GDI's is kept in off-screen memory that DirectDraw also hands
  out.

## Readings, not findings

The netbook record's reading fits here too: the tiles and font look
sampled from texture memory something overwrote with screen rows. On the
2164W the textures are first lost, so a candidate is the window between
DirectDraw freeing them and the ICD's next `IsLost` check, in which the
CPU-drawn pictures or the remade copies could be filled from, or into,
memory DirectDraw has handed to someone else. Nothing measured shows it.

## Next, when picked up

1. With the menu on screen and corrupt, run `C:\V9XTRCN.EXE`, then fetch
   `V9XSNA7.INI` and `V9XGL.LOG` before leaving the menu.
2. Log, per ICD interval, which paths the menu's batches took and whether
   any copy was found lost during the menu itself.
3. Read back one CPU-path texture (its level 0) while the menu shows, to
   tell a wrong texture from a wrong draw.
