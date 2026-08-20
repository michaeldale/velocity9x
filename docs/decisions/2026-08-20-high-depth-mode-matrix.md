# 32-bpp and 1280x1024 mode matrix, both S3 chips

Date: 2026-08-20
Status: accepted for the 86Box targets; physical Trio64 outstanding

First guest run of the 24/32-bpp work. One `s3` binary, build
`d32bpp-a5625d4`, deployed to both 86Box targets and driven through the whole
declared mode list.

## Results

Both targets: **11/11 `enable-ok`, GDI PASS on every mode, palette PASS on
every 8-bpp mode.** Four of the eleven are new.

| Mode | ViRGE/DX `:9869` | Trio64 `:9871` |
|---|---|---|
| 640x480x8, 800x600x8, 1024x768x8 | pass | pass |
| **1280x1024x8** | **pass** | **pass** |
| 640x480x16, 800x600x16, 1024x768x16 | pass | pass |
| **1280x1024x16** | **pass** | **pass** |
| **640x480x32** | **pass** | **pass** |
| **800x600x32** | **pass** | **pass** |
| **1024x768x32** | **pass** | **pass** |

Results under `build\driver-results\mode-matrix-s3-virge-dx-20260820-174432`
and `...-s3-trio64-20260820-175345`. Both chips ran from the same image, which
is the claim the merged family exists to make.

Screenshots at 1024x768x32, 800x600x32 and 1280x1024x16 were inspected, not
just collected: correct desktop and icon colours, correct geometry, no
shredding and no channel transposition.

That last point is the one worth stating precisely, because it is the evidence
for the change most likely to fail silently. The PDEVICE flag fork used to claim
`FIVE6FIVE` at any depth above 8 bpp. Had that survived, the DIB engine would
have packed three channels into the first two bytes of every 32-bpp pixel, and
the corruption would be visible *within* the GDI path - so a GDI screenshot
showing correct colours does test it.

## Confirmed on the real display output, 2026-08-20

The gap this section described is closed. Host-side `PrintWindow` captures of
the 86Box windows - outside the guest's GDI entirely, so sharing none of the
driver's assumptions about pitch, base or depth:

| Target | Mode | Real display |
|---|---|---|
| ViRGE/DX `:9869` | 1024x768x32 | clean, correct colours and geometry |
| Trio64 `:9871` | 1024x768x32 | clean |
| ViRGE/DX `:9869` | 1280x1024x16 | clean, window 1280x1093 |

The driver's own `Surface=` line agreed at each: `pitch=4096 dwb=4096 dds=4096
debpp=32`, and `pitch=2560 dwb=2560 dds=2560 debpp=16` at 1280x1024.

So 32 bpp and the new 1280x1024 resolution are established on the S3 chips by
something other than GDI agreeing with itself. That matters because the same
technique, applied to the Mach64 the same day, showed shredded noise behind a
GDI capture that looked perfect (D5).

`PrintWindow` needs the 86Box window **not minimised**; a minimised one has a
0x0 client rect and yields nothing.

## What this still does not establish


**Read D5 in `docs\issues\2026-08-16-tier0-defects-deferred.md` before reading
the table above as proof the display works.** Every check in the matrix is
GDI-side - resolution from GDI, drawing through GDI, palette read back through
GDI, screenshot a GDI blit - so all of them share the pitch, base and depth the
driver chose and are self-consistent whatever the CRTC is actually scanning out.
Six modes once passed on the Mach64 while the monitor showed noise.

The check that would settle it is a capture that does not pass through the
guest's GDI: a host-side `PrintWindow` of the 86Box window. It could not be
taken here - all three 86Box windows were minimised, so `GetClientRect` returns
0x0 and `PrintWindow` yields nothing, and restoring somebody's windows to get a
screenshot is not the tool's business. **Still outstanding, on a non-minimised
emulator window.**

Also not covered:

- **The 2 MiB physical Trio64 - done, 2026-08-20.** The branch driver installs
  and reaches `enable-ok` on BARRY; 800x600x32, 640x480x32 and 1280x1024x8 all
  display correctly; and 1024x768x32 (3 MiB) and 1280x1024x16 (2.5 MiB) are
  refused with `DISP_CHANGE_BADMODE`. So the new `ValidateMode` VRAM check does
  what it was added for, on the card it was added for.

  Read `docs/issues/2026-08-20-barry-tiling-was-a-screenshot-race.md` before
  trusting any screenshot taken there. A capture on that machine immediately
  after a mode change catches the desktop mid-repaint and is indistinguishable
  from a stride bug; it produced a day of wrong conclusions, including a
  regression report that had to be withdrawn.
- **Doom95 at 640x400x8.** The row moved from index 3 to index 4 of the mode
  table when 1280x1024x8 was inserted before it. Nothing reads the table by
  index except `modes[0]`, so this should be inert, but the regression has not
  been re-run.
- **The blitter declines.** Both S3 engines now refuse above 16 bpp and the CPU
  path serves those depths. No DirectDraw workload was run at 24 or 32 bpp, so
  the decline is asserted by construction and not yet observed.
- **Direct3D**, which is 16-bpp gated and untouched, was not re-run.
