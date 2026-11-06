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
