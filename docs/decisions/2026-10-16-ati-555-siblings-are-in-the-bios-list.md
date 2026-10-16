# The Rage XL's 16 bpp modes all have 15 bpp twins; the driver now finds them in the scan

Date: 2026-10-16

Status: Measured on A8U4I5 (ATI 3D Rage XL PCI, 1002:4752), boots 483
and 484. Closes
[2026-10-16-ati-16bpp-modes-without-a-555-sibling](../issues/2026-10-16-ati-16bpp-modes-without-a-555-sibling.md).
Evidence: `docs/probe/a8u4i5-rage-xl-555-sibling-2026-10-16/`.

## What the BIOS lists

The issue assumed 400x300 and 512x384 had no 15 bpp mode. The boot 483
scan cache (`V9XBOOT-boot483.INI`, 42 modes listed, 40 cached) says
otherwise. Every 16 bpp mode has a 15 bpp twin numbered one below it:

| Geometry | 15 bpp | 16 bpp | Stride |
|---|---|---|---|
| 320x200 | 010D | 010E | 640 |
| 320x240 | 0213 | 0214 | 640 |
| 400x300 | 0233 | 0234 | 800 |
| 512x384 | 0223 | 0224 | 1024 |
| 640x350 | 0243 | 0244 | 1280 |
| 640x480 | 0110 | 0111 | 1280 |
| 800x600 | 0113 | 0114 | 1600 |
| 1024x768 | 0116 | 0117 | 2048 |
| 1280x1024 | 0119 | 011A | 2560 |
| 1600x1200 | 0303 | 0304 | 3200 |

The 15 bpp twins report `BitsPerPixel` 15 with 5:5:5 masks, the 16 bpp
modes 5:6:5. All are linear (attributes 00BB, 1600x1200 00BA).

`v9x_vbe_mode_555` only knows the VESA pairs up to 011A, so the rows
numbered by the BIOS (0214, 0234, 0224, 0244) were published 5:6:5 on a
5:5:5 desktop (`V9XMODES-boot483.INI`, rows 0E to 11). 1600x1200 is not
in the driver's table. The 15 bpp twin was there all along; the driver
had no way to find it.

## The change

- **`vbe_modes.c`**: `v9x_vbe_sibling_555` returns the VESA pair when
  there is one. Otherwise it searches the scan cache. A twin is accepted
  when the scan lists the asked mode as 5:6:5 and the twin:
  - has the same width, height and BIOS stride,
  - is 5:5:5 in two bytes a pixel,
  - is supported, with a linear framebuffer.

  The stride is compared between the two BIOS entries, not against the
  driver's row, because a family may have widened the row's pitch since.
- **`modes16.c`**: the layout decision, the published masks and the new
  `v9x_modes16_row_555_mode` all take the sibling from that function.
- **`ddi.c`**: the mode set programs the number it returns.

There is no ATI-specific code. Any BIOS that lists its own 15/16 pairs
gets the same treatment.

The host test (`test_555_sibling_from_scan`) was written first and
failed on the two OEM pairs before the lookup existed.

## The CRTC route was not tried

The plan was to scan a 16 bpp mode out as 5:5:5 by rewriting
CRTC_GEN_CNTL's pixel-width field after the mode set. With a BIOS mode
that does this already, a register poke would only reproduce, without
the BIOS's timing, what 4F02h with the 15 bpp number sets. So it was
not measured. Whether the Mach64 CRTC can change pixel width on its own
is still unknown, and nothing here needs the answer.

## A/B on A8U4I5

| | Boot 483 (2f817177) | Boot 484 (this change) |
|---|---|---|
| 512x384x16, 400x300x16 masks | F800/07E0/001F | 7C00/03E0/001F |
| Assault Rigs menu device | target formats 00000002, "the display is 5:6:5" | target formats 00000004, no warning |
| Wipeout title | noise (`../a8u4i5-rage-xl-wipeout-2026-10-16/title-512x384-565.png`) | clean (`wipeout-title-512x384-555.png`) |

- **Wipeout, boot 484**: all four device opens described a 5:5:5
  target. 2,647,072 triangles drawn, none refused. The agent's
  screenshot of the 512x384 title shows the backdrop and logo in their
  colours; the in-race ship is correct too.
- **Assault Rigs, boot 484**: the 3DCIF log moved from 565 to 555 at
  the menu, and 125,729 triangles drew with none refused. The agent's
  screenshot is black at 400x300 before and after (the game flips), so
  **nobody has looked at the screen yet.** Whether the empty in-game
  panel from the issue is fixed is not known.

## Not done

- **Assault Rigs on the monitor**: Michael looked after this change.
  The menu still shows "boxes", as before; at 5:6:5 (boot 485) the
  colours were worse. Filed and parked:
  [2026-10-16-assault-rigs-menu-boxes](../issues/2026-10-16-assault-rigs-menu-boxes.md).
- **The tester's report** of DirectDraw saying 5:6:5 while the screen
  showed 5:5:5 at 640x480 (Rage Pro) is a separate case and stays open.

Gates: build-host, run-checks.
