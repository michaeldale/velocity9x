# Updater cycles on the ViRGE/DX guest

Date: 2026-10-09. Machine: Win86SE 86Box guest (S3 ViRGE/DX 86C375,
Win98 4.10.2222 A, agent 0.9.1). Plan:
`docs/plans/optional-update-checker-and-auto-updater.md`, phase 5 gate.

## Setup

- A local fixture served what the v9x_update_checker plugin serves (check
  reply, `SIGNED.TXT`, notes, the zip) from the host, reached from the guest
  as `http://10.0.2.2:8000` through `V9XUPD.EXE /SERVER=`.
- Each "release" was the current tree built with `build.h` temporarily
  numbered 0.15.1, 0.15.2 and 0.15.3, zipped by .NET as a release is, and
  signed with the committed test key; the installed `V9XUPD.EXE` was a
  `build-update.ps1 -TestKey` build.
- The guest's Velocity9x install predated the `V9xFamily` marker. It was
  given `V9xFamily="s3"` and `InfSection="V9x.Install.virge-dx"` by
  `REGEDIT /S`, as a current INF install writes them.

## What was measured

- `InfPath` holds a short name, `VELOCI~1.INF`, and the file is in
  `C:\WINDOWS\INF\OTHER`, not `C:\WINDOWS\INF`.
- Cycle 1 (0.15.0 to 0.15.1): consent, check, signature, notes, download
  (397,081 bytes), staging. `WININIT.INI` held seven `dest=src` lines in
  8.3 form and no `NUL=`. The staged SHA-256s equalled the package files'.
  After the restart all seven installed files hashed to the staged ones and
  `V9XMODES.INI` reported build `fixture0151`.
- `GLIDE2X.DLL` was not staged: the guest has 3dfx's Glide 2.56.00.0459,
  newer than ours by version resource, and the INF's flag 40 keeps it. That
  is the intended outcome.
- The registry half applied as SetupX would: `DEFAULT` and `MODES` were
  rebuilt from the new INF (32 bpp and 1280x1024 rows appeared), and
  `DEFAULT\Mode` went back to the INF's `8,640,480`. The desktop still came
  up at 1024x768, as Windows keeps the live mode elsewhere.
- **Finding:** the first `/FINISH` showed its result with `MessageBox` from
  `RunOnce`. Windows 98 runs `RunOnce` entries under "Windows 98 Setup"
  before the desktop and waits for each, so boot stopped until the box was
  clicked (Michael saw it on the guest's console; the agent's screenshot of
  the desktop did not show it). `/FINISH` now checks and records silently
  and starts `/RESULT`, which waits for `Shell_TrayWnd` and then shows the
  outcome once.
- Cycle 2 (0.15.1 to 0.15.2): the staging updater was the 0.15.1 fixture,
  which still wrote its record to `[Velocity9xUpdate]`. That section was
  renamed to `[Velocity9xInstall]` in the same change, because an install
  cleared it along with the saved `ReportUrl`. So the 0.15.2 `/FINISH`
  found no record and did nothing. The renames had happened (hashes
  matched) and boot completed unattended. No shipped build wrote the old
  section.
- Cycle 3 (0.15.2 to 0.15.3), staged and finished by the current code:
  boot reached the desktop unattended, then "Velocity9x was updated to
  0.15.3 (build fixture0153). The driver now running reports build
  fixture0153" appeared on the desktop.

## The physical machine: A8U4I5

A8U4I5 (P3, Matrox Millennium MGA-2064W, boot 373) against the same kind
of fixture, served on the LAN (`/SERVER=http://10.11.6.137:8000`), with
the family `matrox`.

- Six display class keys carry `V9xFamily` there (vbe, two ati, s3, sis,
  matrox) from earlier cards. The updater chose `Display\0011`, the one a
  present device uses.
- **Finding:** that key's `InfSection` was `Velocity9x.Install`, from an
  older Matrox package; the current INF calls the model
  `V9x.Install.mga2064w`. Planning by `InfSection` alone would have refused
  the update. The updater now finds the model as SetupX does, by the key's
  `MatchingDeviceId` (`PCI\VEN_102B&DEV_0519`) in the new INF's models
  section, falls back to `InfSection` only when no model lists the ID, and
  writes the section it used back to `InfSection`.
- Eight files staged, `GLIDE2X.DLL` among them: A8U4I5's was our own older
  build, so flag 40 replaced it, where the ViRGE guest's 3dfx one was kept.
- Boot 374 reached the desktop unattended. `/RESULT` then reported 0.15.1
  build `fixture0151`, with the running driver reporting the same build.
  `InfSection` reads `V9x.Install.mga2064w`.
- A8U4I5 is left on the fixture build with a test-key `V9XUPD.EXE`.

## The production server

The plugin change is deployed: `/v9update/signed/velocity9x/0.14.0`
answers the plugin's own plain-text 404 (an unknown path gets Apache's HTML
404), as it should for a release with no `SIGNED.TXT`. The check reply is
unchanged until a release carries one.

## Not measured

- An end-to-end update through the production server: no release has a
  `SIGNED.TXT` yet. The first will be 0.15.0.
- A failed update's path (a hash mismatch after restart), and the Winsock
  fallback.
- The guest still runs the 0.15.3 fixture build with a test-key
  `V9XUPD.EXE`; the next ordinary deploy replaces it.
