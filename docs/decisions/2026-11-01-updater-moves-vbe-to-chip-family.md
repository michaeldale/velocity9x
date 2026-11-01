# The updater moves a card off vbe to its chip family

Date: 2026-11-01. Trigger: report V9X-AW3C75 (M-XP7053, Radeon R7E
1002:9874). Plan: `docs/plans/optional-update-checker-and-auto-updater.md`.

## What the report showed

V9XUPD took M-XP7053 from 0.32.0 to 0.34.0 and installed the `vbe`
package: `V9XHW.INI` read `Adapter=Generic VESA adapter`, `Acceleration=none`,
`Direct3DMode=software`, so 3DMark 2001 refused to start. Not a regression in
the ati code. The machine's driver key said `V9xFamily=vbe` (the signed
0.32.0 vbe package went on by Have Disk at boot 17; every later dev ati
build was a WININIT rename over it), and the updater chose the package by
that value alone. 0.34.0's ati package lists 9874. Any user who installed
vbe on a card a chip family later claims would stay on vbe the same way.

## The change

- `SIGNED.TXT` gains `HardwareIds=` under each `[Family.x]`, signed, from
  the package index. Updaters before this change ignore the key (they read
  `File`, `Size`, `Sha256` by name). 0.34.0's file would grow from 1188 to
  2661 bytes; installed updaters accept up to 16384.
- `v9x_release_family_for`: a card installed on `vbe` moves to the one
  other family whose list names its `PCI\VEN_xxxx&DEV_xxxx`. Never chip
  family to chip family, never on two claimants, never to an unlisted card.
  Host tests cover each case and were seen failing before the code existed.
- The check asks the server again with the new family (download URLs carry
  per-file ids, so the client cannot derive one) and holds that reply to
  the signed file as before. The install picks the new INF's model by the
  card's PCI ID and writes it to `MatchingDeviceId`; the INF's AddReg
  writes the new `V9xFamily`. `V9XUPD.INI` records `FromFamily`/`ToFamily`.
- The model's text from the new INF (`v9x_inf_model_description`) goes to
  the driver key's `DriverDesc` and the devnode's `DeviceDesc` under
  `HKLM\Enum`, the two names SetupX sets; Device Manager lists the second.
  The old `DeviceDesc` is added to the backup's `REGISTRY.REG`.

## Measured on the Mach64 VT2 guest

`Win98SE-Mach64VT2` (86Box, ATI Mach64 VT2 1002:5654, agent 0.14.0), port
9873. Files in `docs/probe/updater-family-switch-2026-11-01/`.

- Setup, boot 133: the 0.35.0 vbe driver files by WININIT rename, a
  `build-update.ps1 -TestKey` V9XUPD, the vbe INF over `VELOCI~1.INF`, and
  `V9xFamily=vbe`, `InfSection=Velocity9x.Install.Manual`,
  `MatchingDeviceId=PCI\CC_0300` by `REGEDIT /S` - what a Have Disk manual
  vbe install records. `V9XHW.INI` then read the report's state:
  `Adapter=Generic VESA adapter`, `VendorId=unclaimed`.
- Fixture: a local stand-in for the plugin at `10.0.2.2:8000`, release
  0.35.1 (the tree with `build.h` set to 0.35.1, build id `fixture0351`),
  ati and vbe zips, signed with the test key.
- The request log shows the check with `family=vbe`, `SIGNED.TXT`, a
  second check with `family=ati`, the notes, then only
  `velocity9x-0.35.1-ati.zip` downloaded.
- The offer read "It moves this card from the generic driver to the ati
  family" on two lines (`boot133-offer.png`).
- Staged: eight files, each SHA-256 equal to the fixture ati package's
  (`V9XDISP.DRV` 01fbc5b1..., `V9XHAL.DLL` ba5087ad..., `ATI3DCIF.DLL`
  f87e0ab6...). `WININIT.INI` held only `dest=src` lines. `GLIDE2X.DLL` and
  `GLIDE3X.DLL` were not staged: the guest's 3dfx copies are newer by
  version, as the INF's flag 40 intends; both stay in `C:\V9XNEW`.
- Driver key before the restart: `V9xFamily=ati`,
  `InfSection=V9x.Install.mach64-vt2`,
  `MatchingDeviceId=PCI\VEN_1002&DEV_5654`.
- Boot 134 reached the desktop unattended. `/FINISH` found every file at
  its staged hash: `State=committed`, `RunningBuild=fixture0351`.
  `V9XHW.INI`: `Adapter=ATI Mach64 VT2 264VT2`, `VendorId=1002`,
  `DeviceId=5654`. `/RESULT` showed "updated to 0.35.1 (build fixture0351)".
  `Acceleration=none` is this guest's tier-0 ati path, as before the test.
- **Finding:** `DriverDesc` kept the vbe model's text. The setup above also
  left the devnode's `DeviceDesc` at the ati name, so it could not show
  that Device Manager would keep the vbe name too.

## Second run: the names (boots 135-138)

The same cycle with the description fix, fixture build `fixture0353`, and
the setup extended to set the devnode's `DeviceDesc` to the vbe text as a
Have Disk vbe install does (`setup-ASVBE.REG`).

- Boot 137 on vbe: both names read "Velocity9x VBE-generic display (any
  VESA VBE 2.0+ adapter)". The same five requests as before, `family=ati`
  second, only the ati zip downloaded.
- Staged: `DriverDesc` and `DeviceDesc` both "Velocity9x ATI Mach64 VT2";
  `REGISTRY.REG` in the backup holds the old `DeviceDesc` line.
- The restart went through the updater's own Yes this time. Boot 138:
  `State=committed`, `RunningBuild=fixture0353`, `Adapter=ATI Mach64 VT2
  264VT2`, and Device Manager lists "Velocity9x ATI Mach64 VT2" under
  Display adapters (`boot138-device-manager.png`).
- A first attempt at the boot 136 setup could not replace `V9XUPD.EXE`: the
  previous boot's `/RESULT` box was still open, so the file was in use. It
  was put again at boot 137 and its SHA-256 checked before the run.

## Open

- Not run against the production server: the plugin's handling of the
  extra `HardwareIds=` line when it mirrors `SIGNED.TXT` is unchecked.
- The move ships with the updater, so it reaches a vbe install only from the
  release after the one that first contains it. M-XP7053 (0.34.0's V9XUPD)
  needs the ati package by Have Disk.
- The guest is left on the fixture0353 ati build with that build's V9XUPD.
