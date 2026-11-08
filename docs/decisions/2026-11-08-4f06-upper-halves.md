# The stock 1.03-08 4F06h changes the upper halves of EAX, ECX and EDX

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only.

## Summary

On the S3 1.03-08 BIOS of the Trio64V+, the VBE scan-line call 4F06h leaves
the upper halves of EAX, ECX and EDX different from what the caller
passed: the BIOS's INT 10h entry saves only the 16-bit registers while the
function computes in 32-bit ones. A 32-bit caller that keeps values in
those upper halves across the call loses them.

## The measurement

On the S3 1.03-08 base of the Trio64V+, bits 31-16 of EAX, ECX and EDX are
not what the caller passed after INT 10h AX = 4F06h: the INT 10h entry saves
only the 16-bit registers while 4F06h computes in 32-bit ones, so the caller
gets the upper halves back as 4F06h left them. Measured in the emulator
([#108](https://github.com/perchrh/trio64-vbios-tools/issues/108)). The
stock 2.04.07 keeps them.

## What it means here

Not applicable. This driver's 4F06h callers are 8086-compiled C through
`#pragma aux` (`vbe16.c`), which holds no 32-bit state in those registers,
and the ring-0 mini-VDD never calls 4F06h. Recorded so the fact is known if
a 32-bit caller is ever added.

## References

- trio64-vbios-tools issue
  [#108](https://github.com/perchrh/trio64-vbios-tools/issues/108), the
  emulator test (`windows`) that observed the upper halves.
- VBE 1.2 (VS911022), section 6: the caller's registers other than the
  outputs are to be preserved - the BIOS deviates.
