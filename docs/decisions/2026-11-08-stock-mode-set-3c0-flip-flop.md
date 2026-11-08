# The stock mode set leaves the 3C0h attribute flip-flop unknown

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only.

The VGA attribute controller at port 3C0h alternates between index and data
on every write; a flip-flop inside the chip decides which one the next write
is, and a read of the input status register (3DAh, or 3BAh in the mono
modes) resets it to index. The stock 2.04.07, 1.03-08 and 1.5-07 may end a
mode set with the flip-flop pointing at data
([#52](https://github.com/perchrh/trio64-vbios-tools/issues/52)); a
program that writes 3C0h right after a mode set, without reading 3DAh
first, sends its index as data and its data as the next index. The usual
symptom is a wrong palette.

## What it means here

No module here writes 3C0h directly today, so this is a footnote for
whoever does: read 3DAh first after any 4F02h.
