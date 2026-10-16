# 86Box Mach64 VT2: Check for updates from 0.18.2 to 0.18.5, 2026-10-16

Step 7 of [RELEASING.md](../../RELEASING.md) for 0.18.5, on the guest used
for 0.18.0 to 0.18.2
([probe](../86box-mach64vt2-update-0.18.0-to-0.18.1-2026-10-15/README.md)).

- **Guest:** 86Box `Win98SE-Mach64VT2`, agent rel-0.6.0 on host port 9873,
  boots 126 to 127, running the 0.18.2 `ati` files (`State=committed`,
  `To=0.18.2` before).
- **Server:** 0.18.5 mirrored from the GitHub release by
  `updates_publish_github_release` (release 10, sequence 10, eight files).
  Its one warning, that the survey zip is not in `SIGNED.TXT`, is
  expected. `check?...&version=0.18.4&family=s3` replied `status=update`,
  `latest=0.18.5` and a `signed=` line; the served `SIGNED.TXT` and the
  `s3` and `ati` zips equal `releases/0.18.5/` by SHA-256.

## The update

Started with `V9XUPD.EXE /CHECK` rather than the Velocity9x tab; the
dialogs are the same.

1. The privacy prompt named version 0.18.2 and the ati family; Yes.
2. **Offer** (`offer.png`): "0.18.5 is available. This computer has
   0.18.2, for the ati family", with the release notes.
3. **Update now** downloaded and checked the package and asked to restart
   (`ready-to-install.png`); the backup went to
   `C:\V9XDIAG\UPDATE\BACKUP\V0_18_2`.
4. **Staged** (`WININIT-staged.INI`): eight `[rename]` lines from
   `C:\V9XNEW`, no `NUL=` line.
5. **After the restart (boot 127):** "Velocity9x was updated to 0.18.5
   (build 295bb549). The driver now running reports build 295bb549"
   (`updated.png`).

## Results

- `V9XUPD.INI`: `State=committed`, `From=0.18.2`, `To=0.18.5`,
  `RunningBuild=295bb549`, eight files.
- The eight recorded SHA-256s equal the files in
  `releases/0.18.5/velocity9x-0.18.5-ati.zip`.
- `WININIT.INI` was consumed.

Not checked: Send report, a rollback from the backup, and the update on
physical hardware.
