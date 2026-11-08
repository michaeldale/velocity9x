# 4F03h never returns bit 15, even when the mode was set with it

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only.

VBE 1.2 says the memory clear bit is not returned, and the stock BIOSes
follow: 4F03h returns the 15-bit mode number with bit 15 clear, even after
a 4F02h that set the mode with the no-clear bit (measured in the emulator,
[spec-c-vbe, VBE-48](https://github.com/perchrh/trio64-vbios-tools/blob/main/docs/cleanroom/spec-c-vbe.md)).

## What it means here

The mini-VDD captures the entry mode with 4F03h and puts it back at the end
of its sweep (`loader.asm`): if that restore ever goes through 4F02h with
the returned BX, the memory clears. The no-clear intent does not
round-trip; it has to be re-added by the caller.
