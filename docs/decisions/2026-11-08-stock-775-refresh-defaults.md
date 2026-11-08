# The stock 2.04.07 refresh defaults, and the 4F14h rate interface

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only.

Measured on the stock S3 2.04.07 (Trio64V2/DX), emulator and card
([#64](https://github.com/perchrh/trio64-vbios-tools/issues/64),
[#65](https://github.com/perchrh/trio64-vbios-tools/issues/65),
[#56](https://github.com/perchrh/trio64-vbios-tools/issues/56)):

- CR41 = 08h and CR52 = 80h select interlaced timings: 107h (1280x1024)
  about 87 Hz fields at 46.7 kHz, 120h (1600x1200) about 98 Hz fields at
  63.7 kHz. LCDs and many CRTs report out of range. Windows 98's own S3
  driver picks no rate itself; it gets the interlaced default.
- Confirmed on a card: with the stock ROM, a monitor reports out of range
  at 1280x1024 and 1600x1200.
- The 640x480 tables run 792 or 796 pixels per line instead of VESA's 800,
  so one LCD preset cannot cover the class.
- Mode 120h displays 1201 lines (CR12 = B0h) while 4F01 reports 1200.

The 2.04 BIOSes offer the S3 OEM function 4F14h to choose a rate: BL = 01h
sets the rate the next set of a mode uses, BH = 01h reads it, BH = 02h lists
the rates (chip-rules, "S3 OEM functions (4F14h)",
[chip-rules.md](https://github.com/perchrh/trio64-vbios-tools/blob/main/docs/chip-rules.md)).
No Trio64 or Trio64V+ BIOS offers 4F14h.

## What it means here

This driver, like S3's, sets no rate, so on a stock V2 ROM the
high-resolution modes come up at whatever CR41/CR52 hold. 4F14h BL = 01h is
a route to 60 Hz on the V2, if the family ever wants it.
