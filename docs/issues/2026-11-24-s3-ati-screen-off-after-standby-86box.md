# s3 and ati: after Stand By the screen stays off (86Box)

Date: 2026-11-24. Status: open, cause not found. Found on 86Box only.

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
