# Assault Rigs: Michael sees "boxes" on the menu, at 5:5:5 as at 5:6:5

Date: 2026-10-16. Status: parked (minor, Michael). Seen on A8U4I5's Rage
XL, boots 484 and 485, with the 15 bpp sibling fix (5a732f78).

## What was seen

After the menu's 400x300 mode moved to 5:5:5
([decision](../decisions/2026-10-16-ati-555-siblings-are-in-the-bios-list.md)),
Michael watched the monitor: the menu is "same as before, boxes". What
the boxes lack, and what they should show, was not described.

The menu is built from 3D blocks with the item names on their faces
("1 PLAYER", "SELECT LEVEL", "PASSWORD"). They fly in over the backdrop
with the logo, and the game then drops into an attract demo. The agent's
screenshot misses most flipped frames, but bursts of eight caught menu
frames at both layouts:

- **Boot 484, 5:5:5**: backdrop and three stacked blocks with no text
  and no logo.
- **Boot 485, 5:6:5 (`HighColor=16`)**: the logo and "1 PLAYER" while
  the blocks fly in; "SELECT LEVEL" and "PASSWORD" on the level menu;
  a frame with one block and almost no text.

These are animation frames, so a missing label may be a face turned
away rather than a fault. Comparing single frames proves nothing either
way.

## What is established

- **The colour layout is not the cause.** The boxes look the same at
  both layouts, and at 5:6:5 Michael reported worse colours than at
  5:5:5, on the same build. 5:5:5 stays.
- **Textures are not rewritten after TextureReg.** The DLL copies each
  texture at registration (its texels are in video memory, level0 from
  D43E8300 onward; 52 registrations, no unregistration). A probe ran on
  boot 484: every 30 frames it compared each registered level with the
  copy, for 430 menu frames. It found no difference. The probe was not
  kept.
- **Nothing is refused or skipped.** 388,177 triangles over those 430
  frames, none refused, and no skip or approximation lines in the log.

## Next, if picked up

- Ask what the boxes should show. A photo of the monitor next to ATI's
  own driver running the same menu would settle it.
- Log the state values of a menu frame: the census logs pointers, not
  values. Look for a texture op, alpha or composite state the menu sets
  and the label draws depend on.
