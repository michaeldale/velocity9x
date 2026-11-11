# The stock 1.03-08 4F02h accepts modes that do not fit the memory

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only.

## Summary

On the S3 1.03-08 BIOS of the Trio64V+, 4F02h accepts mode numbers whose
picture does not fit the installed video memory, while 4F01h on the same
BIOS marks those modes unsupported. A driver that trusts 4F02h alone can
set a mode the card cannot hold; a driver that trusts 4F01h alone is safe.

## The measurement

The S3 1.03-08 base of the Trio64V+ sets VESA modes with 4F02h even when the
picture does not fit the video memory, while 4F01h marks those same modes
unsupported (ModeAttributes bit 0 clear): with 2 MB, 118h, 119h and 11Ah;
with 1 MB also 107h, 112h, 115h-117h and 120h. Measured in the emulator
([#107](https://github.com/perchrh/trio64-vbios-tools/issues/107)). Its
4F02h does not run the memory comparison its 4F01h runs.

## What it means here

Already handled. `ValidateMode` measures each row against the card's own
VRAM, and the VBE 1.2 runtime scan hides rows the BIOS does not report, so
the failure mode does not reach 4F02h here. Recorded because it is the
reason a 4F01h "supported" answer cannot be trusted alone on these BIOSes.

## References

- trio64-vbios-tools issue
  [#107](https://github.com/perchrh/trio64-vbios-tools/issues/107), which
  names the code addresses: the 4F01h compare at C000:4898, the 4F02h
  entry at C000:48E1.
- VBE 1.2 (VS911022), section 6.2: ModeAttributes bit 0, "mode supported
  in hardware".
