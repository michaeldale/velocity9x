# s3 and ati: after Stand By the screen stays off (86Box)

Date: 2026-11-24. Status: fixed on 86Box for `s3` and `ati` (Mach64 VT2)
the same day (see the end); physical cards not run. Found on 86Box only.

## Symptom

On 86Box (ym430tx, APM), Start > Shut Down > Stand By, then the
emulator's Resume: with the `s3` package on a ViRGE/DX and the `ati`
package on a Mach64 VT2 the screen stays off ("Monitor in sleep mode")
after pointer moves, a DirectDraw program, minutes of waiting and, on
the Mach64, a mode set to 800x600 and back. Windows runs on, and GDI and
DirectDraw draw correctly into the frame buffer. With S3's own driver and
ATI's own driver on the same cards the desktop comes back. With `vbe` and
`matrox` on a Millennium it comes back too (`matrox` after some seconds).

## Known

- Windows calls none of our monitor-power callbacks across Stand By:
  `GetMonitorPowerStateCaps` reports D0 only since
  [2026-08-11](../decisions/2026-08-11-monitor-power-management.md).
- The only VESA calls reaching the mini-VDD are four 4F15h and one 4F02h
  (`-VesaTrace` on the ViRGE). No 4F10h.

## Not known

What turns the scanout off (the APM BIOS is the first suspect), which
register 86Box reads as sleep for each card, and what the vendors'
drivers do on wake. Whether physical cards behave the same.

## Next

Read 86Box's sleep condition for the ViRGE and the Mach64; then, on the
S3 clone, trace a build that advertises D1-D3 and see whether Windows
calls D0 on resume and the screen returns, and whether that brings back
the 2026-08-11 monitor-timeout blank. B9 still needs a laptop.

Record: [2026-11-24-1.0-b5-b9-86box.md](../decisions/2026-11-24-1.0-b5-b9-86box.md).

## 2026-11-24, later: s3 fixed on 86Box, ati open

86Box reads the ViRGE as asleep while SR0D & 50h or CR56 & 06h is set,
the Mach64 while CRTC_GEN_CNTL & 0Ch is set. With monitor D0-D3
advertised (an instrument build), Windows calls low before Stand By and
D0 after Resume on both cards. On the ViRGE the S3 D0 body clears the
bits and the desktop returns; `s3` builds now advertise D0-D3. On the
Mach64 our D0 writes nothing and the screen stays off, so the bits are set
by something outside our hooks, still not identified.
[Record](../decisions/2026-11-24-standby-monitor-power-s3-ati.md).

## 2026-11-24, evening: ati fixed on 86Box

The Mach64 now has a D0 body (CRTC_GEN_CNTL bits 2-3 through block I/O
BAR1 + 1Ch). Windows called the monitor callbacks across Stand By on one
Mach64 boot in four, so the D0 also runs after every VBE 4F02h mode set,
which is how the display driver re-enables after each resume. Stand By
twice and a monitor cycle on the VT2: desktop back each time.
[Record](../decisions/2026-11-24-standby-monitor-power-s3-ati.md).

Still open: physical cards (S3, Mach64 class), Radeons (other registers;
not run after Stand By), what sets the bits across Stand By, and why the
Mach64 boots differ. B9 still needs a laptop.

## 1.0 re-read, 2026-11-26

Closed. The fault was reproduced on 86Box and the fix measured there the
same day for `s3` (ViRGE/DX) and `ati` (Mach64 VT2). Standby on physical
hardware stays with blocker B9.
