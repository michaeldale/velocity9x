# vbe on a Radeon 9200: sixteen illegible thumbnails of the screen

Date: 2026-11-13
Status: open. Field report only: no files, no photo, not reproduced. No
RV280 card is on any of our machines.

Source: a user comment relayed by Michael on 2026-11-13; where it was
posted was not given. The reporter's words:

> I tried version 0.48, but it displayed 16 illegible thumbnails of the
> screen simultaneously; I'm going to try version 0.49 instead.

The card is a Radeon 9200 on the `vbe` package. Michael thinks the
system is Windows 95; the reporter did not say. The desktop mode, the
Windows build and whether this was the first boot after the install are
not known.

This may be the reporter of the two RV280 surveys
([`../decisions/2026-11-13-rv280-radeon-9200se-9250-survey.md`](../decisions/2026-11-13-rv280-radeon-9200se-9250-survey.md)),
who said `vbe` "doesn't work very well" on a 9200SE and a 9250 without a
symptom. Not confirmed. If it is, the machine is a Pentium III on a VIA
PM133, and the card's BIOS is VBE 2.0 with 50 linear modes at
`D0000000`, 16 MiB reported.

## What 0.49.0 changes for this card

Nothing found. `git log v0.48.0..v0.49.0` has no commit on the VBE path,
and no `ati` row claims an RV280 id, so the card stays on `vbe`. The
same result on 0.49.0 is expected; the reporter's own retry will say.

## Reading of the symptom

Unmeasured. Several small copies of the desktop usually mean the driver
draws with one frame-buffer geometry (bpp or pitch) while the CRTC scans
out another. A driver writing 8 bpp rows into a 32 bpp mode gives four
narrow copies across; that alone does not give a 4x4 grid, and no
arithmetic here explains sixteen. "Sixteen" may also be a rough count.

## Hypotheses

1. **The BIOS set a different mode than the one the driver believes.**
   A VBE 2.0 ATI BIOS that accepts the mode-set call and lands on
   another depth or pitch, or a mode-info block whose bytes-per-scan-line
   is wrong for the linear mode. `V9XBOOT.INI` and `V9XHW.INI` from the
   bad boot would show what was asked for and what the mode-info block
   said.
2. **A 16-colour first boot.** Windows 95 stores 4 bpp after a driver
   change more readily than Windows 98
   ([`2026-11-07-first-install-stays-at-16-colours.md`](2026-11-07-first-install-stays-at-16-colours.md)).
   On a good path that row hands the display to `vga.drv` and draws a
   normal 16-colour desktop, so this fits only if something of ours
   still touched the mode. How the `vbe` driver behaves when the stored
   depth is 4 was not checked.
3. **Installed on the second head.** The card's function 1
   (`1002:5D44` on the 9200SE, class 0380) may be offered as a display
   device. What our INF and Windows 95 do with it was not checked.
4. **The monitor.** No CRTC block on VBE 2.0, so the BIOS picks the
   refresh rate. An out-of-range mode gives a rolling or blank picture
   on most CRTs, not tidy copies, so this is the weakest.

## Owed

From the reporter:
- the Windows version (95 retail, OSR2, 98) and the package version;
- the desktop mode at the time, whether it was the first boot after the
  install, and whether 640x480 at 256 colours shows it too;
- whether any colour depth draws correctly;
- a photo of the screen;
- `C:\V9XDIAG\V9XBOOT.INI`, `V9XHW.INI` and `V9XDD.INI` from a bad boot;
- the result on 0.49.0.

Here: nothing can be measured without an RV280 card or the files.
