# G41 GMA X4500 (`8086:2E32`) claimed as the G45's alias, from one survey

Date: 2026-11-28. Source: GitHub issue 34, a `V9XSURV` report (survey
build `b3996f07`, 0.60.0) from an Acer board with a Pentium E5800. The
reporter runs the `intel-gma` package on an HD 4600 and found it does not
bind here: `8086:2E32` was not in the package.

## What the survey shows

- Function 0 is `8086:2E32` revision 03, class 030000, the only display
  device.
- BAR0 `FE400004`: 64-bit memory, as the G45's register BAR. BAR2
  `D000000C`: 64-bit prefetchable, where VBE's linear frame buffer is.
  BAR4 I/O at `EC00`. The G45 and Q45 surveys
  ([record](2026-10-23-g4x-gma4500-from-surveys.md)) have the same form.
- GGC (config 52h) reads `0350`.
- VBE 3.0, "Intel(r)Eaglelake Graphics Chip Accelerated VGA BIOS",
  32 MiB (511 x 64 KiB).

Linux `intel-gtt.c` treats the G41 as `IS_G4X` with the G45 and Q45, so
the GTT is at BAR0 + 2 MiB and the 3D engine is the G45's.

## What changed

`8086:2E32` is the G45's alias, as `0046` is the `0042`'s: its own
`V9X_HW16_DEVICE` (`v9x_g41_x4500_device`) with the G45's fill engine and
write-combining, the backend's id list, both comparisons in
`V9xPciReadIntelGttConfig`, the backend registry and the INF (an alias in
`family.psd1`). Same design, same fix.

## Not known

Nothing has run on a G41. The GM45 and the G45-class field report
(issue 3) are the nearest evidence. The BIOS lists 640x480 to 1920x1440
at 8, 16 and 32 bpp, 1280x1024 included, which is the attached monitor's
(a Dell 19-inch panel by EDID).
