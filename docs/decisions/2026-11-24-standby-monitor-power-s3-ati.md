# Stand By on 86Box: s3 advertises monitor D0-D3 again, ati clears the Mach64 sync disables after every mode set; both screens come back

Date: 2026-11-24. Machine: 86Box 6.0 build 9001, clones of
`Win98SE-Native-S3` (ym430tx, APM, no ACPI): `Win98SE-B10-S3d` (ViRGE/DX)
and `Win98SE-B10-Mach64` (Mach64 VT2), host port 9879, COM1 captured from
the emulator's pipe. Emulator evidence only. Supersedes
[2026-08-11-monitor-power-management.md](2026-08-11-monitor-power-management.md)
for the `s3` and `ati` families; every other family keeps that decision.

## Problem

After Start > Shut Down > Stand By and the emulator's Resume, the `s3`
(ViRGE/DX) and `ati` (Mach64 VT2) screens stayed off, 86Box reporting
"Monitor in sleep mode", where the vendors' drivers came back
([issue](../issues/2026-11-24-s3-ati-screen-off-after-standby-86box.md)).
The mini-VDD advertised monitor D0 only, by the 2026-08-11 decision, so
Windows never called `SET_MONITOR_POWER_STATE`.

## What 86Box treats as sleep

From the 86Box source (GitHub `master`, read 2026-11-24, not necessarily
build 9001's): the ViRGE is asleep while SR0D & 50h or CR56 & 06h is set
(`vid_s3_virge.c`), the Mach64 while CRTC_GEN_CNTL & 0Ch (the horizontal
and vertical sync disables) is set (`vid_ati_mach64.c`,
`mach64_update_dpms`). Our S3 `V9xMini_Set_Dpms` writes exactly SR0D bits
4 and 6 and, on D0, clears CR56[2:1] and SR01[5]. Outside S3 builds it was
a bare `ret`, and nothing of ours wrote CRTC_GEN_CNTL: the VT2 is a VBE-only
part in the ati driver.

## Measured, s3

1. **Trial build** (`-MonitorPowerTrial`, D0-D3 advertised;
   [`s3-trial/`](../probe/1.0-b9-standby-monitor-power-2026-11-24/s3-trial/)):
   COM1 shows `monitor-power low`, `D0`, `low` on the way into Stand By and
   `D0` after Resume, followed by the display driver's re-enable (4F15h DDC
   reads, two 4F02h 8116h). The desktop is back after the resume. V9XPWR
   (SC_MONITORPOWER off for 2.5 s, then on) passes and the desktop is back:
   the 2026-08-11 failure did not reproduce here.
2. **The change below** (build `s3pwrfix`;
   [`s3-fix/`](../probe/1.0-b9-standby-monitor-power-2026-11-24/s3-fix/)):
   Stand By and Resume, desktop back; V9XPWR PASS, desktop back; a capture
   1.2 s into V9XPWR's off window shows the screen blank with "Monitor in
   sleep mode", so the low state works too. The callbacks arrived on both
   ViRGE boots (91 and 93).

## Measured, ati

1. **Trial build, boot 90**
   ([`ati-trial/`](../probe/1.0-b9-standby-monitor-power-2026-11-24/ati-trial/)):
   the same low/D0/low/D0 calls arrived across Stand By, and the screen
   stayed off. Our ati D0 wrote nothing, so the sync-disable bits were set by
   something outside our hooks (not identified; the APM BIOS is still the
   first suspect).
2. **A D0 body for the Mach64** (`V9xMini_Ati_Set_Dpms`: CRTC_GEN_CNTL at
   block I/O BAR1 + 1Ch, bits 2-3, as xf86-video-mach64's
   `ATIMach64SetDPMSMode`; the port is found at Device_Init, COM1
   `ati-dpms mach64`), advertising D0-D3 when found
   ([`ati-fix/`](../probe/1.0-b9-standby-monitor-power-2026-11-24/ati-fix/)):
   boot 92, screen still off after Stand By, and COM1 shows **no**
   monitor-power call. A trace of the caps callback (boot 94) shows Windows
   asked once at boot and was given 000Fh, then called nothing across Stand
   By or V9XPWR. The original trial configuration rebuilt with the same
   trace (boot 95) got no calls either. So on this guest Windows called
   `SET_MONITOR_POWER_STATE` across Stand By on one Mach64 boot in four
   (90, not 92, 94 or 95); why is not known. V9XPWR blanked and restored
   the screen anyway on boot 93 ("59 Hz", not "Monitor in sleep mode",
   during the off window), by a route that is not ours.
3. What every resume did show, on every boot: the display driver's
   re-enable through VBE 4F02h, which passes our VESA post hook. **The
   change below** (build `atipwrf2`, boot 96): after any 4F02h the post
   hook calls the Mach64 D0. Stand By and Resume twice, desktop back both
   times; V9XPWR PASS, desktop back. No monitor-power callback arrived on
   that boot: the mode-set path is what restored it.

## Decision

- S3 builds (`V9X_S3_DPMS`) report D0-D3 from
  `MiniVDD_GetMonitorPowerStateCaps`.
- ati builds (`V9X_ATI_DPMS`) find the first Mach64-class function
  (VT2/VT3/VT4 and the ids of `V9xAtiChipIds`) with an I/O BAR1 at
  Device_Init. When one is found they report D0-D3, write its sync disables
  on the power callbacks, and clear them after every 4F02h mode set. A
  Radeon is not found and keeps D0 only.
- Every other family keeps D0 only.
- `-MonitorPowerTrial` stays as an instrument (D0-D3 in any family but a
  default ati build). Trace builds (`-VesaTrace`) also log what the caps
  callback returned.

The 2026-08-11 record withheld the low states after a display lost on a
forced monitor off/on, "even after directly clearing the S3 SR0D, CR56, and
VGA SR01 blanking controls". That did not reproduce on this guest; what
differs from August (emulator version, the CR38/CR39 unlock now in
`Set_Dpms`, the display driver) was not isolated.

## Not covered

- Physical cards: no S3 or Mach64-class card was run. On them a monitor
  that sleeps by the power scheme's timeout now goes through these paths.
- The idle timeout itself: only the SC_MONITORPOWER broadcast was run.
- The Mach64 ids other than the VT2: bound by the shared CRTC_GEN_CNTL
  layout, not run.
- Radeons (Rage 128 and later) after Stand By: their sync controls are in
  other registers; nothing here changes them.
- What sets the sync disables across Stand By, and why Windows calls the
  monitor callbacks on some Mach64 boots and not others.
- B9's acceptance (a laptop) is not met by this.

Gates: `run-checks.ps1` (see the commit).
