# Mode sync after a Device Manager reinstall: Windows reinstalls from a stale INF entry, and the sync finds no marked key

Date: 2026-11-08

Status: reproduced on one 86Box guest, one run; fixed in
`settings_syncmodes.c` and the fix verified on the same guest, one run
(see "The fix"). Whether this is the cause on the reporter's machine is
not established; the questions below were posted on issue 15.

Field report: [GitHub issue 15](https://github.com/michaeldale/velocity9x/issues/15),
comment of 2026-11-08 02:08 UTC (FirePro W9000, X99, `vbe` 0.39.0 build
`186b656c`). The reporter replaced SoftGPU's VESA driver through Display
Properties (broken install), removed the device in Device Manager and
rebooted (Velocity9x came back, but only the INF's seven modes showed,
across restarts), then edited the INF and reinstalled (their modes
showed). Their `V9XSYNC.INI` was written after the INF edit, reads
`Status=ok Instance=0000`, and says nothing about the failing step.

## Guest

`Win98SE-BX-Voodoo3` (86Box `686bx`, Voodoo3 3000 AGP `121A:0005`, host
port 9874), agent updated 0.5.2 to 0.16.0 for the run. Starting state:
Windows' "Standard PCI Graphics Adapter (VGA)" on `Display\0004`, plus
leftover class keys from earlier cards, two of them old Velocity9x
Trio64 installs (`0002`, `0003`, no `V9xFamily` value). Package: 0.42.0
`vbe`, build `59b93e48`, pushed to `C:\V9XPKG`. Disk backed up first to
`Velocity9x Backups\Win98SE-BX-Voodoo3-pre-softswitch-20261108` and
restored from it afterwards (hash checked). Registry exports and
screenshots are in this session's scratchpad, not kept.

## Step 1: switch by Display Properties (Have Disk)

Display Properties, Advanced, Adapter, Change, Have Disk `C:\V9XPKG`,
the "any VESA VBE 2.0+ adapter" model. The wizard **reused the
device's existing class key**: `Display\0004` now has `DriverDesc`
"Velocity9x VBE-generic display ...", `V9xFamily=vbe`, and the Run
value `V9xSyncModes` was added.

First boot after the install (boot 286):

    [Velocity9xSync]
    Build=59b93e48
    Instance=0004
    ... 7 keep, 11 add ...
    Generation=4
    Status=ok

Display Properties offered up to 1280x1024, a dynamic row. The switch
path works on the first boot, with no second restart.

## Step 2: remove in Device Manager, restart

Device Manager, Display adapters, the Velocity9x entry, Remove. That
deleted class key `0004` and the Voodoo3 devnode; the Run value stayed.
On the restart Windows redetected the card and restarted once more by
itself (boot 287 to 288). The new state:

    [HKLM\...\Class\Display\0004]
    "InfPath"="VELOCI~1.INF"
    "InfSection"="Velocity9x.Install.trio64"
    "DriverDate"="11- 8-2026"
    "DriverDesc"="Velocity9x S3 Trio32/64 86C764"
    "MatchingDeviceId"="PCI\\VEN_5333&DEV_8811"
    (no V9xFamily; DEFAULT has PCIRebalance=1, which the vbe INF does
    not write)

    [Velocity9xSync]
    Build=59b93e48
    Status=no-op
    Reason=no-marked-instance

`V9XMODES.INI` from the same boot is complete (`Family=vbe`,
`Generation=5`): the `vbe` driver loaded, because the Trio64 section
names the same `v9xdisp.drv`. Display Properties named the adapter
"Velocity9x S3 Trio32/64 86C764" and stopped at 1024x768: the INF's
modes only, the reporter's symptom.

## What it means

`C:\WINDOWS\INF\OTHER` held one Velocity9x INF, today's `vbe` file, as
`Velocity9x ProjectVELOCITY9X.INF`. Every Velocity9x package installs
under that same name, so the Trio64 package that was there before had
been overwritten. The section Windows applied,
`Velocity9x.Install.trio64`, is not in the file on disk, and its
`MatchingDeviceId` is a Trio64 ID on a Voodoo3. Windows took the driver
from its cached INF data (Win98 keeps a driver index, `DRVIDX.BIN` /
`DRVDATA.BIN`), not from the current file. The exact cache mechanism
was not examined.

The sync then did what it was written to do: no key carries this
family's marker, so it records a no-op. Its two assumptions, that the
INF that installed the device is the `vbe` INF and that the INF wrote
the marker, both fail when Windows picks a stale entry.

## Disputed or narrowed

- My first reading of issue 15 was that the sync skips the liveness
  check when only one key is marked. Wrong: `v9x_instance_has_devnode`
  runs after `v9x_find_marked_instance` for that case
  (`settings_syncmodes.c`, the `marked-instance-has-no-devnode` path).
- "Have Disk over another vendor's driver breaks the sync": not on this
  guest. It worked on the first boot.

## Not known

- Whether the reporter's step 2 went the same way. It needs a stale
  Velocity9x entry, which means an earlier Velocity9x package on that
  machine. Ask: the adapter's name in Device Manager after step 2,
  whether another Velocity9x package was ever installed, and
  `DIR C:\WINDOWS\INF\OTHER`.
- Whether SoftGPU's INF matters. This run started from Windows' VGA
  driver, not SoftGPU; SoftGPU was not downloaded.
- Why the reporter's step 1 left a broken install. Not reproduced.

## The fix

Agreed with Michael the same day. When no class key carries the
family's marker, the sync now takes the one unmarked key that a present
device uses (the live Configuration Manager tree, as the several-marked
case already does) and whose `DEFAULT\drv` is `v9xdisp.drv`. None or
several such keys is a recorded no-op (`no-marked-instance`,
`multiple-unmarked-live-instances`). A key marked for another family is
never taken. The report says `Chose=unmarked-live-instance`.

The proposal also said to stamp `V9xFamily` on the key it takes. That
part was left out: the updater (`v9x_install_find`) selects installs by
the same marker, and this key's `InfSection` and `MatchingDeviceId`
come from the stale section (here a Trio64 ID on a Voodoo3). Stamped,
the updater would accept the install and then refuse it with "The new
INF has no model for this card". Unstamped, the updater is unchanged and
the sync repeats the fallback every boot.

Verified on the same guest from the restored backup, build
`4d1aae6b-dirty` (0.43.0 tree plus this change), same steps:

- After Have Disk and one boot: `Instance=0004`, 11 adds, `Status=ok`,
  no `Chose=` line (the marked path, unchanged).
- After Device Manager remove: Windows again reinstalled from the stale
  `Velocity9x.Install.trio64` section, unmarked. This time a
  Plug and Play Monitor wizard held the desktop until answered by hand,
  and the Run entries waited for it. Then:

      Chose=unmarked-live-instance
      Instance=0004
      ... 7 keep, 11 add ...
      Generation=5
      Status=ok

  Display Properties, still naming the adapter "Velocity9x S3
  Trio32/64 86C764", offered 1280x1024.

Gates: `check-tree.ps1`, `build-host.ps1` and `run-checks.ps1` pass.
The guest's disk was restored from the backup afterwards.
