# vbe on a Radeon 9200: sixteen illegible thumbnails of the screen

Date: 2026-11-13
Status: **fixed on main (0.53.0), confirmed on the reporter's 9250 on
2026-11-15.** Our `vbe` driver is also bound to the card's second head,
so Windows 98 ran a second instance on the same card when the Settings
tab opened. A driver guard now refuses that instance. Reproduced and
fixed in 86Box, then run on M-HNBJNV, where the owner reports the screen
stays good (the sections at the end). Field report, no RV280 card on
our own machines. Files and photos arrived 2026-11-14 (section below): a
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

## 2026-11-14: the reporter's answers

Relayed by Michael:

> I don't have the ATI driver installed—only Velocity and WinVBE—but
> this issue was already occurring before I installed WinVBE.
> I restarted my PC a couple of times to try and reproduce the 16
> thumbnails.
> The issue only happens when I attempt to change the display settings,
> so I'm unable to test other resolutions or color depths.

- **No ATI driver installed.** The leftover-ATI-software candidate
  loses its support. The `6E94` and `6E80` escapes are then from
  something else, and what sends them is not known. The A8U4I5 match
  was a weak basis: one machine, and nothing recorded there says ATI
  software was installed either.
- **"WinVBE" is installed too.** What it is (another display driver, a
  utility) and what it installs was not checked here. The fault predates
  it, by the reporter's account.
- **The three loads were restarts.**
- **`MarkedInstances=3`** is consistent with an install history of more
  than one card and driver
  ([`2026-10-03-several-velocity9x-display-instances-block-mode-sync.md`](2026-10-03-several-velocity9x-display-instances-block-mode-sync.md)).
  Since 2026-10-04 the sync picks the live instance when exactly one is
  live; here it stayed a no-op, so none or several of the three counted
  as live. The mode list Display Properties shows is therefore not the
  one in `V9XMODES.INI`. Whether that bears on the fault is not known.
- "Attempt to change the display settings" and the earlier "right when
  you click the Settings tab" may be the same moment or two. Not
  settled.

Checked in the tree: the `vbe` INF binds through `PCI\CC_0300` only, so
it does not claim the card's second function (class 0380) by itself; a
Have Disk install forced onto it would. `ValidateMode`, which the
Settings tab calls for each mode, is a table lookup and a memory check
and makes no BIOS call.

The cause is not known. Still standing: a BIOS call made for the
Settings tab, something in this install's three instances, or WinVBE.

### Owed

- `V9XMSW.EXE /set:800x600x16` from the package, without opening
  Display Properties, and `C:\V9XDIAG\V9XMSW.INI` after it. It says
  whether a live mode switch works on this card, and gives the reporter
  another mode.
- Whether the screen breaks on the click on the Settings tab, or on
  Apply.
- The entries under Display adapters in Device Manager, and what WinVBE
  is.
- If they are willing: remove every display adapter entry in Safe Mode,
  restart, install `vbe` once, and try the Settings tab again.

## 2026-11-15: audit over remote help (M-HNBJNV): the second head carries our driver

The reporter enrolled the machine in remote help (M-HNBJNV, VICO98,
agent 0.17.0). Audited on boot 12. Evidence:
[`../probe/field-radeon-9250-hnbjnv-2026-11-15/`](../probe/field-radeon-9250-hnbjnv-2026-11-15/),
`C:\V9XDIAG` as pulled before the test below, and a screenshot of the
Settings tab. The reporter's three `V9XSURV` files that were also in
that folder are kept out of the repository, as before.

### What the machine holds

- Windows 98 SE (4.10.2222 A, Spanish), Pentium III 997 MHz, 512 MiB.
  The installed build is `3d5a67b1`, the 0.51.0 release. The desktop is
  1024x768x16; `V9XHW.INI` shows pitch 2048 agreed with the BIOS.
- **Both PCI functions of the card are bound to our `vbe` driver:**
  - `1002:5960` (class 0300) is `Display\0003`, matched by `PCI\CC_0300`.
  - `1002:5940`, the second head (class 0380), is `Display\0004`. Its
    `MatchingDeviceId` is `PCI\VEN_1234&DEV_1111&SUBSYS_11001AF4`, the
    QEMU/Bochs VGA row. So a Have Disk pick forced our INF onto it.
  - Device Manager shows both with no problem code.
- Five Display class instances in all, three of ours:
  - `0000`: `sis`, the SiS 6326 row;
  - `0001`, `0002`: WinVBE;
  - `0003`, `0004`: `vbe`.

  Of the three marked instances `V9XSYNC.INI` counts, two are on this
  card and one belongs to a card that is no longer fitted.
- `HKLM\Config\0001\Display\Settings` has a monitor subkey for the
  second head at 640x480x8. It also has subkeys for a GeForce2 MX
  (`10DE:0110`) and the 9200SE's second head (`1002:5D44`), neither of
  which is fitted.

### The Settings tab, run once

At 22:15 guest time, Display Properties was opened and the Settings tab
clicked:
- The tab shows **two monitors**: "1" on the Velocity9x VBE adapter, and
  "2" not attached.
- `V9XTRAIL.TXT` gained a new `libmain`, `query` and `enable-ok` at
  22:15:53, with the desktop already running. A second instance of
  `V9XDISP.DRV` was loaded and enabled for display 2.
- `V9XBOOT.INI` was rewritten by that instance. It shows the same
  1024x768x16 surface and finds the MTRR already set (`MtrrWrite=present`).

The dialog was then cancelled. The screenshot path cannot see the CRT,
so the break itself was not observed here. Within a minute Explorer's
windows were gone with `EXPLORER.EXE` still running, which looks like a
shutdown. The machine went offline at 02:24 UTC, before the `reset`
sent for it reached it. Most likely the owner restarted it after the
screen broke. Not confirmed.

### Reading

Partly measured, partly read from source:
- **Measured:** a second instance of our driver loads and enables on
  the same card when the Settings tab opens. It does that because
  Windows 98 multi-monitor treats the second head as its own display.
- **Read from source:** `V9XHARDWAREDISABLE`
  (`src/display16/runtime.asm`) sets VGA mode 3 with `INT 10h` when an
  instance is disabled.
  - When the dialog releases display 2, that call goes to the one
    physical card, which the primary desktop is still using.
  - The card then scans out a text mode over a frame buffer that
    Windows keeps writing at the graphics geometry. That is the
    80-column coloured cell grid in the 2026-11-14 photos.
- Not observed: the `Disable` call itself. It is not in the trail.

This explains every account so far:
- The fault is tied to the Settings tab and to changing settings.
- The boot is clean.
- The fault predates WinVBE.
- `MarkedInstances=3` is a side effect of the extra bindings, not the
  cause.

Hypothesis 3, "installed on the second head", is now the leading cause,
and the BIOS-call and WinVBE candidates lose their support.

### Fix on the machine (owed, not done)

In Device Manager, remove or disable the second "Velocity9x VBE-generic
display" entry (`1002:5940`). When Windows offers a driver for that
function again, leave it with no driver. Then restart and open the
Settings tab: it should show one monitor and leave the screen intact.

### Driver side (not decided)

The `vbe` package cannot drive a second head. Two options, neither
tested:
- Its 16-bit driver could refuse to enable when a primary instance is
  already live on the same adapter.
- Its INF could carry an explicit no-driver entry for class 0380
  functions.

The QEMU row matching the 9250's second head shows that a Have Disk
install can force any row onto any device.

## 2026-11-15: reproduced in 86Box, and a driver guard

Evidence: [`../probe/dualhead-86box-2026-11-15/`](../probe/dualhead-86box-2026-11-15/).

### The guest

`Win98SE-DualHead` (host port 9876) is a clone of `Win98SE-Mach64VT2`:
- An ATI Mach64 VT2 is the primary, on the `ati` family.
- 86Box 6.0's `gfxcard_2` adds a ViRGE/DX, the second "86Box Monitor"
  window.
- Windows 98 SE gave the ViRGE its own S3 driver. `dualbind.reg` then
  bound that devnode to a copy of our class key, `Display\0004`, which
  loads `V9XDISP.DRV`. That is the 9250's arrangement on two cards:
  two Display devnodes, one driver.

### Before: the older ati build, without the guard

That build's `V9XDISP.DRV` is the 0.35.1-numbered fixture already on
the guest. The Settings tab, clicked once, showed two monitors. Within
about 10 s the primary's emulated screen dropped to VGA text mode,
70 Hz and 720x400 (`before-settings-tab-primary-text-mode.png`), with
Windows still running behind it. That is the fault the 9250's owner
photographed, now reproduced.

### After: `123e978e-dirty`, with the guard

Changes:
- `src/display16/ddi.c`: the first instance to reach `enable-ok` adds a
  global atom and deletes it in Disable.
- An instance that finds the atom without having added it refuses the
  GDIINFO query and Enable before touching hardware. It writes
  `refused-second-display` to `V9XTRAIL.TXT` only, so `Stage=` in
  `V9XBOOT.INI` stays the owner's.
- The atom functions are USER exports, which a display driver must not
  import, so they are looked up by name at the first query.

The new package was installed by `update-associated-driver.ps1`, with
the `V9XDISP.DRV` hash checked on the guest. Boot 163:
- **Settings tab:** two monitors are shown, and monitor 2 is offered
  but not active. The primary stays at 1024x768, 60 Hz
  (`after-settings-tab-primary-intact.png`). The trail has `libmain`
  then `refused-second-display` at 14:21:43.
- **Live mode switches on the primary:** `V9XMSW /set:800x600x16`, then
  back to 1024x768x16. Each gives `enable-start` and `enable-ok`, and
  the agent reads the new size.
- **Settings tab again:** refused again at 14:22:50, primary intact
  (`after-mode-switches-settings-tab.png`).

### Not tested

- The release of the atom in Disable, at shutdown or exit to MS-DOS
  mode, followed by a new owner in the same session.
- A guest with two cards that Windows' own drivers run.

## 2026-11-15: on the reporter's 9250 (M-HNBJNV)

Evidence: [`../probe/field-radeon-9250-hnbjnv-2026-11-15/guard/`](../probe/field-radeon-9250-hnbjnv-2026-11-15/guard/).

### First install: `123e978e-dirty` (the code of d03a1ab1)

Installed as the eight-file `vbe` set by WININIT renames.
- **The first attempt fell short.** One relay call timed out while
  staging, and three files, `V9XMINI.VXD`, `V9XHAL.DLL` and
  `V9XSETP.DLL`, kept their 0.51.0 copies; the other five were the new
  build. The three were staged again one by one, each copy
  hash-checked before the rename was scheduled. After boot 16 all eight
  matched.
- **Settings tab:** clicked at 00:21:26. The trail has `libmain` then
  `refused-second-display`, and the desktop stayed at 1024x768.
- **The owner's report,** relayed by Michael: "display is good".
- **A flaw:** `V9XBOOT.INI` read `Stage=libmain`. The refused instance
  still wrote its LibMain marker over the owner's `enable-ok`, which
  makes a healthy boot read as unconfirmed.

### Second install: `d03a1ab1-dirty`, the code of the follow-up commit

What changed:
- LibMain checks the atom too. An instance that finds another owner
  writes only to the trail, and leaves `Stage=` to the owner.
- A missing USER no longer latches the lookup off. The boot's first
  instance loads before USER, so it now looks again later.

All eight files were staged with hash checks; boot 17. The Settings
tab gave two refusals, at 00:33:18 and 00:33:28, likely one at the
dialog's open and one at the tab's click. `Stage=enable-ok` held, and
the desktop stayed at 1024x768.

Still on the machine, not addressed: the second head's devnode keeps
our driver, and the leftover SiS, WinVBE and QEMU-row class keys
remain. They no longer break the display.
