# Shutdown shows cyan/black garbage before the Win98 safe-to-turn-off screen

Date: 2026-11-07
Status (2026-11-11): **parked, cause unknown, cosmetic.** Two candidate
fixes were built and neither helped (section "Tests" below). Seen on 0.39.0
and 0.44.0; `V9xHardwareDisable` is unchanged through 0.46.0. Reported and
documented by perchrh in
[GitHub PR 24](https://github.com/michaeldale/velocity9x/pull/24), whose
records these are; its code change was not taken.

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

## First explanation (not borne out)

`Disable` ends in `V9xHardwareDisable` (`src/display16/runtime.asm`), which
does only `INT 10h AX=0003h` (VGA text mode 03). S3 desktop mode sets pass
`V9X_HW16_VBE_NO_CLEAR`, so video memory still holds the last desktop, and
the PR's reading was that the bare mode 03 shows it briefly as text-cell
noise. Its fix set VGA SR1 bit 5 (Screen Off) before the mode set and
cleared it after.

Why that was doubted before testing: the BIOS mode set writes SR1 from its
own table and blanks the screen itself, so the bit set beforehand does not
survive the call, and the garbage lasts a second or two, after the mode set
has finished. `AX=0003h` (bit 7 clear) also clears the text buffer and
reloads the font, so leftover desktop data should show only if those writes
did not reach video memory. The photo fits that: cyan text cells full of
nonsense characters.

## Tests

Two full `s3` packages (0.41.0 development, unsigned), on the reporter's
AMD K6-2 400 (Epox MVP3GM-5) with the Trio64V2/DX:

| Build | Change | Result |
|---|---|---|
| A, `0629a6ed` | the PR's SR1 blank around mode 03 | flash unchanged |
| B, `eafe7a72` | no blank; Disable turns off the linear window (CR58 bit 4) and the engine (CR40 bit 0) before mode 03 | flash unchanged |

Also unchanged on the released 0.44.0. Three cards show it: one 86C775
and two 86C765 with different BIOSes. The reporter's notes:

- **86Box 7.0** (Win98SE, Trio64V2/DX, a Super 7 board, CPU and disk
  slowed to match) does not reproduce it.
- **Not write-combining:** the `s3` family only reads the MTRRs, and this
  machine has none to read (`Mtrr=cpu=000b cap=00000000 n=0`). The
  pedestal fix is off on the V2/DX (`Sr1aFound=1F BlankPedestal=off`).
  Their 0.39.0 `V9XDIAG` files (attached to the PR) show every probe
  passing.
- **Pure DOS, no Windows** (their `VGAGARB2.COM`, attached to the PR): VBE
  mode `8114h` (800x600x16, no clear), all 2 MB filled with cyan through the
  banked window, then a bare `AX=0003h`. No garbage. That clears the BIOS's
  mode 03 when it starts from a plain VBE mode. It does not recreate the
  state the driver leaves (its S3 extension registers, the linear window,
  the mini-VDD, Windows' VDD), so the cause can still be in either the
  driver's state or Windows' shutdown after Disable.

## Compared with the DDK

The DDK's S3 sample (`98DDK\src\display\mini\s3v\DISABLE.ASM:56-140`)
restarts the VDD's port trapping (`INT 2Fh AX=4007h`), sends
`VDD_DRIVER_UNREGISTER`, clears SR9 bit 7 (MMIO only) and sets mode 3. Ours
(`V9XVDDUNREGISTER`, then `V9xHardwareDisable`) does the first two in the
same order and the mode set. The SR9 step does not apply: nothing in this
driver sets SR9, and the V2/DX path (`trio_hw16.c`) opens no memory-mapped
register window.

## If it is picked up again

The next question is whether the garbage comes from our mode 03 or from
Windows after Disable returns. A test build that, after `AX=0003h`, clears
the text window through the BIOS (`AH=06h`, attribute 07h) and waits about
two seconds inside Disable would answer it. Black during the wait and cyan
after means it is later than Disable; cyan during the wait means our mode 03
is not reaching the screen on these cards.

## Not claimed

- That ATI or SiS cards show the same flash (only 765 and 775 were
  reported).
