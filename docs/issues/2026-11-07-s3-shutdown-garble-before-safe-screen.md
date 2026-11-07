# Shutdown shows cyan/black garbage before the Win98 safe-to-turn-off screen

Date: 2026-11-07
Status: fix in source, **unverified on hardware**. Seen on 0.39.0; the
Disable path is unchanged through 0.40.0.

## Symptom

On every Windows 98 shutdown with Velocity9x, the panel shows about one to
two seconds of structured cyan/black vertical garbage, then the normal
"Windows is shutting down" / safe-to-turn-off screen. The reporter expects
a black screen in that gap, as with Microsoft's S3 Trio64 driver.

Seen every time on:

- two S3 Trio64V+ (86C765) cards
- one S3 Trio64V2/DX (86C775) card

Not seen with the stock Win98 S3 Trio64 driver on the same machines and
monitor (Eizo LCD).

Evidence: [`../probe/s3-shutdown-garble-2026-11-07/`](../probe/s3-shutdown-garble-2026-11-07/)
(`shutdown-garble-eizo.jpg`, settings reports from the same 0.39.0 boots).

## Cause

`Disable` ends in `V9xHardwareDisable` (`src/display16/runtime.asm`), which
did only `INT 10h AX=0003h` (VGA text mode 03). S3 desktop mode sets pass
`V9X_HW16_VBE_NO_CLEAR`, so VRAM still holds the last hi-res desktop. A bare
mode 03 then briefly shows that buffer as text-cell noise (or a mid-mode-set
memory map). Stock looking black is a driver difference: Velocity9x forced
the visible transition with no blank.

## Fix (in source)

Shared Disable path: `V9xHardwareDisable` in `src/display16/runtime.asm`
sets VGA SR1 bit 5 (Screen Off) before INT 10h mode 03, then clears it
afterwards. Generic VGA sequencer only; no ATI/SiS/chip-family source
changes. Seen on S3 with VBE no-clear; the blank is harmless policy for
every package that shares this Disable.

Hardware confirmation on a 765 or 775 is still owed: shut down and watch
that the cyan flash is gone and the safe screen still appears.

## Not claimed

- A full VRAM clear before mode 03 (blanking hides the leftover; a clear
  was not measured as necessary).
- That ATI or SiS showed the same flash (only 765/775 were reported).
