# The stock 4F04h save/restore covers ADVFUNC_CNTL

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only.

## Summary

The stock BIOS's VBE save/restore (4F04h, the Super VGA state class) saves
and restores ADVFUNC_CNTL, the register whose "enable enhanced functions"
bit decides whether the Trio64's engine writes reach memory. So a 4F04h
restore is one more path that can write that bit behind a driver, beside
the DOS-box BIOS write this project already found and fixed. Nothing is
owed.

## The measurement

The D3 (Super VGA) class of 4F04h saves and restores ADVFUNC_CNTL (4AE8h)
bits 0, 2, 4 and 5, with the data book's write rules
([spec-c-vbe, VBE-74](https://github.com/perchrh/trio64-vbios-tools/blob/main/docs/cleanroom/spec-c-vbe.md)).

## What it means here

[2026-08-27-gdi-accel-corrupts-display-on-physical-trio64.md](../issues/2026-08-27-gdi-accel-corrupts-display-on-physical-trio64.md)
found that a DOS VM's video BIOS wrote 4AE8h with ENB EHFC clear; the
mini-VDD's shield swallows that write, and the driver re-asserts the bit
per operation. A 4F04h D3 restore is another route to the same register.
Whether a DOS-box switch on Windows 98 runs one was not measured (edited
on taking the record, 2026-11-11: it first said that it does). Either way
the per-operation re-assert covers it; nothing is owed.

## References

- VBE 1.2 (VS911022), section 6.6: the state classes, D0-D3, and "the
  complete Super VGA video state" under CX = 000Fh.
- S3 Trio64 data book, section 11.4.2 and table 11-3: the registers of a
  mode's bitmap; ADVFUNC_CNTL (4AE8h) bits 0, 2, 4, 5.
- trio64-vbios-tools `docs/cleanroom/spec-c-vbe.md`, rule VBE-74, the
  register list of the D3 class.
