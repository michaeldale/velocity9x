# 86Box Mach64 VT2: Check for updates from the released 0.18.0 to 0.18.1, 2026-10-15

The second of the two checks the 0.18.0 release owed
([STATUS](../../STATUS.md)), and the first update taken end to end on a
released build.

- **Guest:** 86Box `Win98SE-Mach64VT2`, agent rel-0.6.0 on host port 9873,
  boots 123-124. It ran the released 0.18.0 `ati` package, installed by
  Have Disk ([probe](../86box-mach64vt2-have-disk-0.18.0-2026-10-15/README.md)).
- **Server:** 0.18.1 mirrored from the GitHub release by
  `updates_publish_github_release` (release 6, sequence 6, eight files).
  Its one warning, that `velocity9x-survey-0.18.1.zip` is not in
  `SIGNED.TXT`, is expected: the survey is not a driver family.

## Server side

- `check?app=velocity9x&version=0.18.0&family=s3` replied
  `status=update`, `latest=0.18.1` and a `signed=` line.
- The served `SIGNED.TXT` and the `s3` and `ati` zips equal
  `releases/0.18.1/` by SHA-256.

## The update

**Route:** Display Properties, Velocity9x tab (`before-0.18.0.png`, version
0.18.0, mini-VDD `de564208`), Check for updates, Yes at the privacy prompt.

1. **Offer** (`offer.png`): "0.18.1 is available. This computer has
   0.18.0, for the ati family", with the release notes.
2. **Update now** downloaded and checked the package, then asked to
   restart (`ready-to-install.png`). The backup went to
   `C:\V9XDIAG\UPDATE\BACKUP\V0_18_0`.
3. **Staged** (`WININIT-staged.INI`): eight `[rename]` lines from
   `C:\V9XNEW`, no `NUL=` line.
4. **After the restart (boot 124)** the updater reported "updated to
   0.18.1 (build 2cbf6777). The driver now running reports build 2cbf6777"
   (`updated.png`).

## Results

- `V9XUPD.INI`: `State=committed`, `From=0.18.0`, `To=0.18.1`,
  `RunningBuild=2cbf6777`, eight files.
- The eight recorded SHA-256s equal the files in
  `releases/0.18.1/velocity9x-0.18.1-ati.zip`: V9XDISP.DRV, V9XMINI.VXD,
  V9XSETP.DLL, V9XHAL.DLL, V9XGL.DLL, ATI3DCIF.DLL, V9XTRACE.EXE and
  V9XUPD.EXE.
- WININIT.INI was consumed. Display Properties opens; the tab reads
  version 0.18.1 and mini-VDD `2cbf6777` (`after-0.18.1.png`).

Not checked: Send report, and a rollback from the backup.

## Again, 0.18.1 to 0.18.2 (boots 124-125)

The same route on the same guest the same day, once 0.18.2 was mirrored
(release 7). The offer named 0.18.1 to 0.18.2 for the ati family, eight
`[rename]` lines were staged with no `NUL=` line, and after the restart
`V9XUPD.INI` read `State=committed`, `From=0.18.1`, `To=0.18.2`,
`RunningBuild=07c31612`. The eight recorded SHA-256s equal the files in
`releases/0.18.2/velocity9x-0.18.2-ati.zip`; WININIT.INI was consumed.
