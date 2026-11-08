# Stock 4F15h (DDC): interrupts off for the whole call, and a PIT write

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only.

Measured on the stock 2.04.07, on the card
([#117](https://github.com/perchrh/trio64-vbios-tools/issues/117)): 4F15h
BL = 00h runs with interrupts off from start to end and takes 66 ms; an
EDID read (BL = 01h) takes longer. The BIOS tick falls behind and a UART
without a FIFO can lose bytes. The 1.03-08 base reprograms PIT channel 0
to the standard rate inside its DDC delay on every call.

## What it means here

The mini-VDD's EDID collection calls 4F15h once at init, at ring 0, after
the mode scan (`loader.asm`). Its ordering already contains a hang. The
two hazards it does not contain: 66 ms or more with interrupts off during
VDD init, and a PIT channel 0 reprogram that leaves the rate standard but
discards whatever the system had programmed.
