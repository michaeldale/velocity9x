# Tier-0 `vbe` on W9QI4E14: installed; the first restart has not come back

Date: 2026-10-21
Status: **open.** The package installed and Windows was restarted. The
machine has not checked in since, and the cause is unknown.

Step 2 of `docs\plans\intel-ivybridge-gen7.md`. The machine and its
pre-install state are in `2026-10-21-ivybridge-w9qi4e14-audit.md`.

Captures: `docs\probe\w9qi4e14-ivb-hd4000-tier0-2026-10-21\`.

## What was installed

- **Package.** `build\win98se-vbe` at `1cc25342`, built in a clean
  worktree (`C:\everything\velocity9x-ivb-tier0`) after `run-checks.ps1`
  passed there. The main checkout had another session's uncommitted
  intel-gma edits, and a package built from it would have carried them.
  `PACKAGE-MANIFEST.TXT` and `PACKAGE-SHA256.TXT` are that build's.
- **Install route.** Pushed to `C:\V9XPKG`, then Device Manager, Update
  Driver, "Display a list", Have Disk, `C:\V9XPKG`. This is the GM965's
  route.
- **Model.** The only one offered was "Velocity9x VBE-generic display (any
  VESA VBE 2.0+ adapter)". That is the INF's `PCI\CC_0300`
  compatible-ID model (`Velocity9x.Install.Manual`). The other model names
  `1234:1111` only.
- **Copy.** Windows copied the files without a prompt and asked to restart.
  The restart was declined in the dialog.
- **`WININIT-after-install.INI`.** Two plain renames,
  `glide2x.dll=glide2x.001` and `glide3x.dll=glide3x.001`, and no `NUL=`
  line. This is the same as the GM965's.

## The restart

At 22:09Z the agent's `reboot` (job `ivb-tier0-boot1`, wait 420 s) went
out through the relay. The agent stopped checking in at 22:09:34Z and the
`reboot` call timed out with exit 20.

A ping every three minutes from 22:15Z to 22:30Z got no session. At
22:55Z the machine was still offline: 45 minutes, with no check-in.

What this does not say, and nothing here can tell apart:

- whether Windows' restart path hangs on this board (the netbook's does);
- whether the first boot with `V9XDISP.DRV` and `V9XMINI.VXD` stopped;
- whether the machine was switched off or lost its network.

The Haswell 4570 takes five minutes or more to boot, but 45 minutes is far
outside that.

## When it is back

1. `C:\V9XDIAG\V9XBOOT.INI`: the furthest lifecycle stage the driver
   reached. `V9XMODES.INI`, `V9XHW.INI` and `C:\BOOTLOG.TXT` if present.
2. A screenshot, and `info` for the mode and bits per pixel.
3. If the owner reports a hang, ask where it stopped: the Windows 98
   splash, a black screen, or the desktop. Also whether a power cycle
   boots to the desktop. Recovery is `RECOVER.TXT` in the package: boot
   Safe Mode and restore the standard VGA driver.

If the boot stopped in the driver, the evidence goes in this doc and the
step is not passed. If it reached `enable-ok`, record the modes, the 4F01h
pitches, `mem=` against the 64 MiB stolen, and whether 1920x1080 is
offered.
