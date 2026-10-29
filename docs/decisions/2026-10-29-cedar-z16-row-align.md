# Cedar's Z16 rows on 32 bytes: a 360-pixel GL window lost every clear and draw

Date: 2026-10-29

Status: **measured, one machine.** M-BXMFA8's Radeon HD 5450 (`1002:68F9`),
boots 42 and 43. Follows Carrizo's
[Quake 3 record](2026-10-29-carrizo-opengl-quake3.md), which found the same
shape there. Captures are in
[`../probe/m-bxmfa8-cedar-z16-rows-2026-10-29/`](../probe/m-bxmfa8-cedar-z16-rows-2026-10-29/).

## The two suspects

Carrizo had two depth bugs, and Cedar's code was checked for both:

- **The whole-surface depth fill.** On Carrizo it filled only each row's
  visible width, and in a tiled buffer the padding belongs to tiles in the
  picture. **Cedar's does not have this bug.** `eng_eg.c` already fills
  `pitch_bytes / bytes_per_pixel` pixels a row, and its height rounded to
  the 8-row tile. The Carrizo record said otherwise; that was a misreading.
- **The Z16 pitch.** `d3d_eg.c` rounded a Z16's rows to 16 bytes, the DB's
  8-pixel tile. The core binds a depth buffer only when its pitch is a
  multiple of the engine's `target_pitch_align`, which is 32 bytes on Cedar
  (`v9x_d3d_set_target`). A width that is an odd multiple of 8 pixels fails
  the bind.

## Boot 42: the pitch, reproduced

This was the set from `18362d1d`, with the current V9XGLP (the scans and
texel reads from Carrizo's work).

- The 400-pixel window is 800 bytes, a multiple of 32. Its rows all read as
  on the Haswell: geometry scans green left and red right, `AlphaSolid` red,
  and all four `Small2x2` texels green.
- The T&L scene resizes the window to 360 pixels, which is 720 bytes. Its
  6 clears and 6 draws all logged `result=1`, which is
  `V9X_R3D_RESULT_INVALID`.
  - The frame (`boot42-V9XTNLA.png`) is plain red: `TnlDrawnPixels=100800`,
    the whole window, with 44,100 outside the viewport.
  - The result was `TnlResult=differ`.

Cedar's Quake 3 at 640 (1280 bytes) and 3DMark 2000 at 1024 never met this.

## The change

A Z16's rows are rounded to `V9X_D3D_EG_DEPTH_ROW_ALIGN`, 32 bytes. That is
16 pixels, still whole 8-pixel tiles. The map's own check stays at 16 bytes,
because that is what the DB needs. The new rows can be wider than the
picture; the whole-surface fill already covers that padding.

## Boot 43

The DRV, VXD, HAL and ICD were all deployed and hash-checked. They are the
whole set built from current main, since a HAL-only swap onto an older
driver here lost the 3D engine before.

- **V9XGLP:**
  - The T&L scene matches: 28,193 pixels drawn, against the Haswell's
    28,196, with none differing and none outside the viewport.
  - No clear or draw failed.
  - The other rows are as at boot 42.
- **V9XD32 `/bpp16`:** PASS.
- **V9XDDP:** all 98 `*Ok` keys are identical to boot 35's.

## Open: V9XD32 at 32 bpp

The owner's desktop is now 1024x768x32. It was 16 bpp at boot 42.

- V9XD32's default run fails before Direct3D starts:
  - `SetDisplayMode(640, 480, 32)` returns `0x88760078`, which is
    DDERR_INVALIDMODE;
  - the result is `Result=NO-MODE`.
- It passed at 32 bpp at boot 34, when the desktop was 16 bpp.
- V9XDDP's mode table has the same count as boot 35's (34 modes), and its
  first 24 entries are the same. The report does not list the other 10.
- No surface exists at that point, so the Z placement cannot be involved.
- Not established: whether the cause is the owner's 32 bpp desktop or the
  20 commits since boot 42. They are mostly Carrizo work, plus that chip's
  entries in the shared ati files.

**Answered** in
[the mode subset record](2026-10-29-cedar-dd-mode-subset.md). The desktop
was the trigger: the rule that guarantees the desktop's mode a slot in
DirectDraw's list evicted 640x480x32.

## Not checked

- Quake 3 and 3DMark 2000 were not run on boot 43.
- Direct3D at 32 bpp, per the open item above.
