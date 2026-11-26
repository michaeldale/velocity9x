# Nothing restores the display after an APM or ACPI resume

Date: 2026-11-06. Feature gap, found in a feature review. Status: open,
nothing written, nothing measured.

## Gap

No module handles the power events Windows 98 sends on suspend and resume.
After a resume the adapter may have lost its mode, its engine state or its
PCI configuration; the driver does not check and does not set the mode
again. Not tested on any machine, so whether any of our machines lose the
picture is unknown.

## Feature

1. The mini-VDD receives the power events (APM through VPOWERD, ACPI on
   98/ME) and, on resume, asks the display driver to check the screen once
   Windows is idle.
2. The driver compares the adapter's state with the selected mode and,
   when it differs, sets the mode again and repaints; if the mode set
   fails, runs the video BIOS initialisation again (repost) and retries.
3. Families with an engine (Intel rings, ATI CP, Rage 128 CCE) restore
   their engine state through a backend hook; the policy of what to check
   stays in host-testable C.

## Acceptance

On one laptop (the netbook or the Gateway), a suspend and resume through
the Start menu returns to an intact desktop, and a DirectDraw program
started after it runs; the record says whether the mode had been lost.

## 2026-11-24: 86Box

Stand By on 86Box pauses the emulator, so the card keeps its power and
the mode cannot be lost there. `vbe` and `matrox` (Millennium) come back
to the desktop and V9XWPRS passes after the resume. `s3` (ViRGE/DX) and
`ati` (Mach64 VT2) leave the screen off while S3's and ATI's own drivers
bring it back
([issue](2026-11-24-s3-ati-screen-off-after-standby-86box.md),
[record](../decisions/2026-11-24-1.0-b5-b9-86box.md)). The laptop run
in the acceptance is still owed.

## 2026-11-25: the Oland R7 250, a desktop on APM

Stand By and resume on W9QI0DAD, `ati`, 1024x768x32: the desktop came
back with the mode kept, and V9XWPRS, V9XD32 and V9XFBCMP pass after it
without a restart. The resume takes minutes and looks like a hang; a
first run was power-cycled before the agent was asked. Windows' own VGA
driver resumes the same way, slowly. Our driver leaves the monitor lit
in Stand By (D0 only, by the 2026-08-11 decision). Nothing of ours
restored anything; the mode was not lost here
([record](../decisions/2026-11-25-1.0-b9-standby-oland.md)). The laptop
run is still owed.

## 1.0 re-read, 2026-11-26

Stays open under 1.0 blocker B9. Measured on 86Box and the Oland R7 250
(see blocker B9 in the 1.0 plan); stays open under B9 until a laptop is
measured.
