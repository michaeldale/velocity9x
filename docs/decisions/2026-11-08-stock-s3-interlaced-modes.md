# The stock Trio64-family BIOSes offer some modes only interlaced

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only.

Measured on the stock Phoenix 1.5-07 (Trio64), emulator, every refresh index
walked ([#68](https://github.com/perchrh/trio64-vbios-tools/issues/68)):

- 106h (1280x1024x4), 118h (1024x768x16M), 119h and 11Ah
  (1280x1024x32K/64K) and 120h (1600x1200) exist only interlaced.
- 1024x768 and 1280x1024 at 8, 15 and 16 bpp default to their lowest
  refresh entry, which is interlaced; LCDs report out of range.
- 800x600 defaults to 56 Hz, not the VESA DMT 60 Hz.

The S3 1.03-08 base of the Trio64V+ shows the same family of faults: its
1600x1200 entry runs interlaced (S3COMPAT warning,
[765-rom](https://github.com/perchrh/trio64-vbios-tools/blob/main/docs/765-rom.md)).

## What it means here

The shared S3 mode table has 0x0118 (1024x768x32) and 0x011A
(1280x1024x16), and nothing in the driver knows about interlace. On a card
whose stock BIOS accepts one of those numbers, 4F02h succeeds and the mode
comes up interlaced; `ValidateMode` and the runtime scan cannot see it. A
4F01h check of the ModeAttributes bits (bit 3, interlaced mode) or a CR42
bit 5 read after the set would catch it. Not urgent: every stock BIOS
measured either refuses 0x0118 on 2 MB cards or runs it interlaced on 4 MB
ones.
