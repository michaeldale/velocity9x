# Installing Velocity9x

Velocity9x is an engineering bring-up driver. A failed install leaves Windows
98 unable to display. **Do not install it on a machine you cannot recover.**

The safest and by far the best-tested environment is an [86Box](https://86box.net/)
virtual machine. Almost all development and verification has been done there.

## 1. Check your hardware

Velocity9x installs on exactly two PCI devices and refuses everything else.

| Chip | PCI ID | Package to build |
|---|---|---|
| S3 ViRGE/DX 86C375 | `5333:8A01` | `build/win98se-s3` |
| S3 Trio32/64 86C764 | `5333:8811` | `build/win98se-s3` |

One package covers both. Its INF declares a model per chip, and the driver
detects which card is fitted when it scans the PCI bus.

In Windows 98, check Device Manager → Display adapters → Properties →
Details for the hardware ID. If it is not one of the two above, stop: the INF
will not match, and forcing it will not work.

Requirements: Windows 98 (Second Edition is what has been tested), the card in
a PCI slot with a working linear framebuffer aperture, and at least 4 MiB of
video memory for the full mode matrix.

## 2. Build the package

See [BUILDING.md](BUILDING.md) for prerequisites. Then:

```powershell
./scripts/build-active-package.ps1
```

Never install from `packaging/win98se` directly — that is the INF source, not
a package. Always install from the built output.

## 3. Back up before you touch anything

This step is not optional. Restoring a cold backup is the only recovery method
that restores all registry and device state.

**On a VM:** shut Windows 98 down completely, fully close every 86Box and
86Box Manager process, then:

```powershell
./scripts/backup-86box-profile.ps1 -ProfilePath "<path to your 86Box profile>"
```

This copies the VHD, `86box.cfg` and the NVR directory, and writes hashes. It
refuses to run while 86Box is open.

**On real hardware:** take a full disk image. At minimum, confirm the machine
boots and displays with the Standard PCI Graphics Adapter (VGA) driver before
you start, and know how to reach Safe Mode.

## 4. Set up a serial log (recommended)

If the driver fails before the desktop appears, the serial log is the only
thing that will tell you why.

In 86Box, configure COM1 as a Named Pipe server named `velocity9x-com1`. On the
host, start the capture before you boot:

```powershell
./scripts/capture-serial-pipe.ps1
```

## 5. Preflight (optional but cheap)

Mount the package directory as a folder CD and run `V9XSTAGE.EXE` from it.
It loads the VxD lifecycle probe and asks the driver only for GDIINFO and mode
validation. It never sets a mode and never installs anything. A PASS means both
binaries load and the driver's query path is coherent.

`V9XSET.EXE` is a read-only status panel and is also safe to open before
installing.

## 6. Install

1. Boot Windows 98 normally.
2. Control Panel → System → Device Manager.
3. Expand **Display adapters** and open your S3 adapter.
4. **Driver** → **Update Driver** → choose a specific driver or location.
5. **Have Disk**, and browse to the built package directory.
6. Select the Velocity9x entry for your chip.
7. Let Windows copy the files. **Do not accept a different device ID** if it
   offers one.
8. When prompted, **shut down fully**. Do not warm-restart the first boot.
9. Start the serial capture, then cold-start the machine once.

## 7. Verify the first boot

The first boot comes up at 640x480 in 256 colours.

If you captured COM1, expect:

```
V9X-MINI init build=<id>
V9X-MINI defaults-ok callbacks=0 build=<id>
V9X-DRV load build=<id>
V9X-DRV enable-query mode=640x480x8
V9X-DRV lfb=0x........ bytes=00400000
V9X-DRV enable-ok mode=640x480x8 lfb-mapped
```

In the guest, `C:\V9XBOOT.INI` records the last stage the driver reached. A
successful activation passes through `enable-ok`.

Then run the framebuffer test:

```
V9XGDI.EXE /auto
```

Without `/auto` it opens an interactive window and waits for you — use `/auto`
for an unattended PASS/FAIL written to `C:\V9XGDI.INI`. It draws GDI
primitives and checks tolerant pixel readback.

**If the desktop does not appear, stop after one boot attempt.** Do not
reboot repeatedly. Go to [Recovery](#recovery).

## 8. Exercise the modes

Use Display Properties, or the bundled exerciser:

```
V9XMSW.EXE /set:800x600x16     switch to one mode and verify it
V9XMSW.EXE /cycle:20           alternate resolutions at the current depth
V9XMSW.EXE /depth:20           alternate 8 and 16 bpp
V9XMSW.EXE /cursor             add cursor agitation around every switch
```

Results are written to `C:\V9XMSW.INI`.

The full matrix is 640x480, 800x600 and 1024x768 at both 8 and 16 bpp, plus
640x400 at 8 bpp. On the ViRGE, resolution and colour-depth changes both apply
live. On the Trio64, live depth switching is built but unverified — accept a
restart prompt if Windows shows one.

Run `V9XPAL.EXE` in an 8-bpp mode to check palette animation and readback, and
`V9XGDI.EXE /auto` after each switch.

## 9. Check DirectDraw and Direct3D

```
V9XDDP.EXE            full DirectDraw and Direct3D probe -> C:\V9XDD.INI
V9XDDP.EXE /pal8      palettized 8-bpp presentation and mode lists
V9XDDP.EXE /status-only   quick blitter-reachable check
```

`Result=COMPLETE` with `HRESULT`s of `0x00000000` is a pass. `V9XDD.INI` also
dumps DirectDraw's own view of the driver, which is what distinguishes an
accepted HAL from a rejected one.

## Recovery

Full details are in `RECOVER.TXT` inside the package. In short:

1. **Preferred:** restore the cold backup you took in step 3.
2. Otherwise, power off, start up and press **F8** before Windows starts, and
   choose **Safe Mode**.
3. Control Panel → System → Device Manager → Display adapters.
4. Remove the Velocity9x adapter entry.
5. Reboot and choose **Standard PCI Graphics Adapter (VGA)** when detected.
6. Only once standard VGA works, and only if Windows no longer references
   them, delete `V9XDISP.DRV` and `V9XMINI.VXD` from `C:\WINDOWS\SYSTEM`.

Do not delete the active Velocity9x files before switching the adapter back to
standard VGA, and do not overwrite a known-good backup with a failed boot.

After a *garbled* live mode switch that did not crash, a plain reboot is
usually enough: the boot-time mode path is unchanged and re-enters the
registry-selected mode.

## Known install-time gotchas

- **Do not install while a fullscreen DirectDraw application is running.** A
  modal dialog behind a fullscreen game can take the Win16 mutex and wedge the
  machine mid-install. Exit games first and confirm they have actually exited.
- **Games configured for 320x200, 320x240 or 640x400 may crash.** DirectDraw
  will not accept 640x400 from the driver, and the ModeX path reports success
  then fails in use. Set such games to 640x480. See
  [issues/2026-08-15-doom95-low-resolution-modes.md](issues/2026-08-15-doom95-low-resolution-modes.md).
- **24-bpp and 32-bpp are not offered.** This is expected, not a fault.
- **The cursor is drawn in software.** There is no hardware cursor.

## What to include in a report

- Chip and PCI ID, and the package build identifier.
- The display mode in use when it went wrong.
- `C:\V9XBOOT.INI`.
- `C:\V9XDD.INI` and `C:\V9XTRACE.INI` for DirectDraw or Direct3D problems.
- `C:\V9XGDI.INI`, `C:\V9XMSW.INI` or `C:\V9XPAL.INI` for a failing test.
- The COM1 serial capture, if the machine never reached the desktop.
