# 4F02h clears all of video memory unless bit 15 is set

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only.

## Summary

A VBE mode set through 4F02h clears the whole of video memory - 4 MB
included - unless bit 15 of the mode number asks it not to. The clear is
the largest single cost of a mode set. This driver already sets bit 15;
this record is the measurement that says that is right.

## The measurement

Measured in the emulator
([#131](https://github.com/perchrh/trio64-vbios-tools/issues/131)): a 4F02h
mode set with the clear bit absent clears all of video memory, 4 MB
included - a traced run burned more than 50 million instructions on the
clear alone.

## What it means here

Nothing to change. `v9x_vbe_mode_flags = 0x8000` and the S3 family's
`V9X_HW16_VBE_NO_CLEAR` are the measured right choice; this is the
measurement behind it.

## References

- trio64-vbios-tools issue
  [#131](https://github.com/perchrh/trio64-vbios-tools/issues/131): the
  traced emulator run that burned more than 50 million instructions on the
  4 MB clear.
- VBE 1.2 (VS911022), section 6.3: bit 15 of the mode number, "the video
  memory is cleared" when it is not set.
