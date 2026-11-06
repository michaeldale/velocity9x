# Only the S3 family has a DPMS body; whether others power the monitor down is unknown

Date: 2026-11-06. Feature gap, found in a feature review. Status: open,
nothing written, nothing measured.

## Gap

`V9xMini_Set_Dpms` (`src/minivdd32/loader.asm`) has a body only in S3
builds (`V9X_S3_DPMS`); every other family's mini-VDD returns without
doing anything, by design, because the S3 sequencer bits are not portable.
Whether Windows' monitor power-down then reaches the monitor on a non-S3
family is not known. The master VDD issues VESA 4F10h itself when the
mini-VDD offers no monitor-power callback (the post hook
`MiniVDD_VESACallPostProcessing` in the same file sees those calls), so
the outcome depends on which callbacks the mini-VDD registers. Not read
through, and not measured on any machine.

## Feature

First, measure: on a VBE-family machine, does the monitor's power light
change when Windows powers it down? If it does, close this record with
that evidence.

Otherwise, for the generic VBE family, and as a default for any family with no chip
hook: VBE/PM, function 4F10h. Query the supported states (BL=00h) once,
then set on, stand-by, suspend and off (BL=01h) from the same four callers.
Families with documented power registers (Intel pipe/port disable, ATI,
Matrox) can override through a backend hook later.

The 4F10h call from the mini-VDD at DPMS time must be safe while Windows
is running; that is to be measured, not assumed.

## Acceptance

On one machine whose BIOS reports 4F10h, Windows' monitor power-down puts
the monitor into stand-by (its power light), and a key wakes it with the
desktop intact; on a BIOS without 4F10h, nothing changes and the boot
record says so.
