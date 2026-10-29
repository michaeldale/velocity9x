# DirectDraw's mode list keeps 640x480, 800x600 and 1024x768 at every depth

Date: 2026-10-29

Status: **measured, one machine.** M-BXMFA8's Radeon HD 5450 (`1002:68F9`),
boots 43 and 44. Follows
[the Cedar Z16 rows record](2026-10-29-cedar-z16-row-align.md), which left
this open. Captures are in
[`../probe/m-bxmfa8-cedar-dd-modes-2026-10-29/`](../probe/m-bxmfa8-cedar-dd-modes-2026-10-29/).

## The failure

On boot 43, V9XD32 could not start at 32 bpp:
- `SetDisplayMode(640, 480, 32)` returned `0x88760078`, DDERR_INVALIDMODE;
- the result was `Result=NO-MODE`.

It had passed at boot 34. The owner's desktop had since moved from
1024x768x16 to 1024x768x32.

## What V9XDDP `/pal8` showed

GDI lists 40 modes, including 640x480x32. DirectDraw lists 33, and none of
them is 640x480x32 or 800x600x32. Its 32 bpp entries:

| Boot | Desktop | DirectDraw's 32 bpp modes |
|---|---|---|
| 3 (`m-bxmfa8-cedar-vbe-2026-10-26/boot3-V9XDD-pal8.INI`) | 16 bpp | 320x200, 320x240, 512x384, 640x350, 720x400, 640x480 |
| 43 | 32 bpp | 320x200, 320x240, 512x384, 640x350, 720x400, 1024x768 |

The cause is `dd16.c`'s subset, not the code since boot 42:
- The shared block holds 32 rows (`V9X_DD_MODE_COUNT`), and this BIOS
  publishes 40.
- `v9x_vbe_dd_subset` took every 8 and 16 bpp row (26 here), then 32 bpp
  by ascending area. That left six 32 bpp slots, and the smallest modes
  took them, with 640x480x32 last.
- `dd16.c` then guarantees the desktop's mode a slot by replacing the last
  pick. With a 16 bpp desktop it was already chosen, so nothing was
  replaced. With 1024x768x32, it evicted 640x480x32.

This was not specific to the HD 5450. Any BIOS that publishes more than 32
modes, with a 32 bpp desktop outside the cut, lost its last 32 bpp pick. The
QEMU test already recorded 1024x768x32 being cut, and described it as
"nothing is wrong with the subset policy".

## The change

`v9x_vbe_dd_subset` now ranks rows:
1. 640x480, 800x600 and 1024x768 at every depth, in table order;
2. the other 8 and 16 bpp rows, in table order;
3. the other 24 and 32 bpp rows, by ascending area.

The desktop rule still replaces the last pick, which is now never a
standard resolution while anything else was chosen.

The function is a single selection loop on one key, the rank and then the
order. Three loops put the intel-gma driver's `_TEXT` 157 bytes over its
57,344-byte budget, which had 27 bytes spare. The single loop is 4 bytes
smaller than the old function (57,313).

Host tests:
- `test_dd_subset_standard_modes_first` is the HD 5450's shape. It failed
  before the change.
- `test_dd_subset` and the QEMU test were updated: 1024x768x32 now makes
  the cut there.

## Boot 44

The full ati set was deployed (DRV, VXD, HAL and ICD), hash-checked.

- DirectDraw's 32 bpp modes are now 640x480, 800x600, 1024x768, 320x200,
  320x240 and 512x384.
- 640x350x32 and 720x400x32 are the modes that lost their slots. GDI still
  offers them.
- V9XD32 PASS at 32 bpp and 16 bpp.
- V9XDDP's 98 `*Ok` keys are identical to boot 35's. The `/pal8` mode
  sets, at 640x480, 640x400, 320x240 and 320x200, all succeed.

## Not checked

- No other machine was run. The change alters only lists longer than the
  block, such as QEMU's.
- No program that wants 640x350 or 720x400 at 32 bpp was tried.
