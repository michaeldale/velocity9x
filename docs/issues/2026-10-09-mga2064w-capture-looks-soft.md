# The MGA-2064W's picture looks soft through the USB HDMI capture

Date: 2026-10-09. A8U4I5, physical MGA-2064W, boots 356-360. Status: parked.

## Symptom

Viewed through the capture chain - VGA into a converter, into a USB HDMI
grabber (`534d:2109`, MS2109, OBS at 1024x768) - text and icon edges look
soft and doubled horizontally under Velocity9x, at 800x600x16 and still at
1024x768x16. Geometry is correct: straight rows, no shear, nothing doubled
vertically. Other cards through the same chain look sharp. Nobody has
looked at the card on a CRT.

## What was measured

`V9XTIME.EXE /mga` (`tools/diag/vga_timing_win32.c`, new for this) reads
CRTC, CRTCEXT and Misc Output without writing anything but the index ports,
and times the vertical retrace for two seconds. Both captures are in
`docs/probe/a8u4i5-mga2064w-timing-2026-10-09/`.

| 1024x768x16 | Velocity9x (BIOS mode) | MGAPDX64 |
|---|---|---|
| Horizontal total | 1344 | 1312 |
| Horizontal sync start | 1048 | 1032 |
| Vertical total | 806 | 800 |
| Vertical sync start | 770 | 768 |
| Sync polarity | -/- | -/- |
| Refresh | 59.60 Hz | 60.84 Hz |
| Line rate | 48.04 kHz | 48.67 kHz |
| Pixel clock (8-dot) | 64.56 MHz | 63.86 MHz |
| CRTCEXT3 | `81h` | `91h` |
| Misc Output | `EFh` | `EDh` |

The BIOS mode is VESA DMT 1024x768@60 to within 0.7% of its clock;
Matrox's driver runs a non-standard total.

## Hypotheses this kills

- **A non-standard horizontal total making the converter sample between
  pixels.** Velocity9x's is the standard one. Matrox's is not, and looks no
  better.
- **Something Velocity9x does that Matrox's driver does not.** Michael
  looked at the capture with MGAPDX64 at the same 1024x768x16 (boot 359):
  it does not look better. An earlier recollection that MGAPDX64 was sharp
  was at its own 800x600 default and was not compared side by side.

## Not established

- Whether the card or the converter is the soft part. A CRT, or another
  VGA capture device, would separate them.
- What CRTCEXT3 bit 4 does on the 2064W. MGAPDX64 sets it; the 1064SG
  specification calls bits 4:3 reserved (p.4-133). With MGAPDX64 no sharper,
  it is no longer a lead for this symptom.
- The TVP3026 RAMDAC and pixel PLL state under either driver: not read.

## Swap files

`TONATIVE.REG` puts `Display\0011` back to MGAPDX64 and `TOV9X.REG` back
to Velocity9x (its `[-...\MODES]` line removes the vendor's extra mode keys;
verified by re-export on boot 360). Import with `REGEDIT /s`, then warm
restart. Both are specific to A8U4I5's class key.
