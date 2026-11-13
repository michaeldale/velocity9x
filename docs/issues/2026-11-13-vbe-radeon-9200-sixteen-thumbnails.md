# vbe on a Radeon 9200: sixteen illegible thumbnails of the screen

Date: 2026-11-13
Status: open. Field report, not reproduced: no RV280 card is on any of
our machines. Files and photos arrived 2026-11-14 (section below): a
Radeon 9250 on Windows 98 SE, 0.49.0 too, clean boot, the screen breaks
when the Settings tab is clicked.

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

## 2026-11-14: the reporter's files and photos, on 0.49.0

Evidence: [`../probe/field-radeon-9250-vbe-2026-11-13/`](../probe/field-radeon-9250-vbe-2026-11-13/),
seven diagnostic files and three photos of the CRT, relayed by Michael.
With them, a second message from the reporter:

> So far, I've only tested it on Windows 98SE; it happens right when you
> click the "Settings" tab in "Display Properties," but when the screen
> becomes distorted, the 16 thumbnails aren't always visible

So: Windows 98 SE, not Windows 95. The card in the files is the Radeon
9250 (`1002:5960`), which makes this the reporter of the RV280 surveys
or someone with the same card. The build is `4357fc87`, which is in
`v0.49.0` and not in `v0.48.0`: 0.49.0 shows it too, as expected.

### What the files show

- **The boot is clean.** `Stage=enable-ok`, `VbeDetail=ok`, mode `0101`
  640x480x8. The driver's pitch (640) and the BIOS's own answer
  (`VbeScanBytes=640`) agree. All 50 linear modes were described; 30
  rows published.
- `Adapter=Generic VESA adapter`, `Acceleration=none`,
  `ScreenShadow=on`, write-combining set on `D0000000` for 16 MiB
  (`MtrrWrite=ok`).
- `V9XSYNC.INI`: `MarkedInstances=3`, `Status=no-op`,
  `Reason=multiple-marked-instances`. Three display driver instances
  carry our mark on this install.
- `V9XESC.INI` has two escapes this driver does not answer and Windows
  does not define: `6E94` (with input `00000000 0000000B 000002DC
  00000000`) and `6E80`. In every other `V9XESC.INI` in `docs/probe`,
  `6E80` and `6E76` appear on one machine only: A8U4I5 while it carried
  an ATI Rage XL. Reading, not established: a 6Exx escape is ATI
  software's private call, so ATI software is installed and talking to
  the display driver on this machine.
- `V9XTRAIL.TXT` has three loads, 09:32:00, 09:33:00 and 09:34:31, each
  to `enable-ok`. The trail is appended across boots, so this is three
  boots or three loads in one session; the files do not say which.

### What the photos show

Taken 09:36 to 09:37, after the last load.

- Not small copies of the desktop. The picture is coarse cells, about
  80 across, each cell holding one repeated shape (a filled box with a
  thin edge, or a vertical bar), in white, black, blue, cyan and
  magenta.
- The third photo has 12 rows of cells with blank lines between the
  rows.

Reading, not measured: that is what a VGA text mode looks like when it
decodes a graphics frame buffer as characters and attributes, with the
font plane overwritten. The card was moved out of the VBE graphics mode,
or part of the way out, while Windows went on drawing at 640x480x8.

### What this does to the hypotheses

1. Mode-set geometry mismatch at boot: not supported. Pitch and scan
   length agree and the desktop is good until the Settings tab is
   clicked.
2. A 16-colour first boot: dead. The desktop is 8 bpp.
3. Second head: open, not addressed by these files.
4. Monitor: dead. The fault is triggered by a click.
5. **New: something reprograms the card when the Settings tab opens.**
   Candidates, none tested:
   - ATI's Display Properties pages or tray software, left from ATI's
     own driver, reaching the chip directly when Display Properties
     loads them. The 6Exx escapes point this way.
   - A BIOS call Windows 98 makes for that tab that this ATI BIOS
     answers by touching the mode.
   The Settings tab opens without this on every `vbe` machine we have,
   so the cause is particular to this card, its BIOS or this install.

Not explained: the "16 thumbnails" the reporter first described. No
photo shows them, and they say it is not always what appears.

### Owed

From the reporter:
- whether ATI software is installed (Add/Remove Programs, a tray icon,
  ATI tabs in Display Properties), and the result with it removed;
- which of the other tabs in Display Properties were clicked first, and
  whether the fault needs the Settings tab or any tab;
- whether the three loads in `V9XTRAIL.TXT` were three restarts;
- a photo of the "16 thumbnails" form if it shows again.

The earlier list's Windows version, mode, photo and files are answered.
