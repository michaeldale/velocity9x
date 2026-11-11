# The stock POST monitor sense can switch grey-scale summing on

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only.

## Summary

At every POST the stock BIOSes sense the monitor by flashing colours and
reading the RAMDAC sense line. If the reading looks monochrome - which an
LCD or a converter can cause - the BIOS switches on grey-scale summing,
and every palette load from then on comes out grey. INT 10h AX = 1200h
BL = 33h switches it off again.

## The measurement

Measured in the emulator with the sense line modelled as a colour monitor, a
monochrome monitor and nothing attached, on the stock 2.04.07, 1.03-08 and
1.5-07 ([#134](https://github.com/perchrh/trio64-vbios-tools/issues/134)):
POST shows black, blue and green in turn, reads the RAMDAC sense comparator
(3C2h bit 4), and on a monochrome reading switches the BIOS state to
grey-scale summing (mode 13h). BIOS palette loads then come out grey. INT
10h AX = 1200h BL = 33h switches it off.

## What it means here

An LCD or a converter that reads as monochrome at POST leaves the palette
summing grey, from the POST screen onward. No module here touches
AX = 1200h. If a palette ever looks washed out on a working panel, this is
the first thing to read.

## References

- trio64-vbios-tools issue
  [#134](https://github.com/perchrh/trio64-vbios-tools/issues/134): the
  emulator runs with the sense line modelled as colour, monochrome and
  nothing attached, on the stock 2.04.07, 1.03-08 and 1.5-07.
- Ralf Brown's Interrupt List (release 61), INT 10h AH = 12h BL = 33h:
  select DAC color summing off (BL = 31h would switch it on).
- IBM VGA documentation: 3C2h bit 4, the sense bit the POST code reads.
