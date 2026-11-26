# Both 86Box S3 guests stuck once at "Windows is shutting down" during the mode matrix

Date: 2026-11-21. Status: seen once on each guest, not reproduced, cause
not looked for.

## What was seen

`scripts/run-vm-mode-matrix.ps1` on the `s3` package built from
`ac9aa76b-dirty` (0.61.0 tree, the driver code of 0.60.0), agents 0.19.0,
86Box 6.0 build 9001. Each mode is written to the registry and the guest
restarted with the agent's `reboot`.

- **Win98SE-Trio64 (port 9871).** Six modes passed. In 1024x768x16 a
  guest program started by the matrix was still running after 180 s
  ("pid ... still running after 180 s (1 process(es)); no fault dialog";
  the 180 s wait is the one on `V9XGDI /accel`), no report was written
  for the mode, and the `reboot` that followed never came back. The
  86Box window showed the Windows 98 "Windows is shutting down" screen
  and the agent's port refused connections.
- **Win86SE, ViRGE/DX (port 9869).** Ten modes passed. 800x600x32 wrote
  its reports and passed; the `reboot` towards 1024x768x32 never came
  back. Same screen, same refused port. No program was reported as
  still running.

Both emulator processes were stopped and started again. Each guest then
booted straight into the mode the registry held (1024x768x16, boot 384;
1024x768x32, boot 706), and the matrix passed for that mode and every
mode not yet reached. All eleven `s3` modes pass on both guests across
the runs.

The Mach64 VT2 guest ran the `ati` matrix the same evening, six modes
and six restarts, with no stop.

## What is not known

- Whether the driver's Disable, the agent's `reboot`, 86Box or Windows
  held the shutdown. Nothing was read from either guest while it was
  stuck: the agent was gone, and the host capture shows only the
  shutdown screen.
- Whether the Trio64's stuck program and its stuck shutdown are one
  fault or two. `V9XGDI /accel` injects a fault and poisons the session
  by design, so every restart in the matrix follows a poisoned session.
- The rate: two stops in about 24 `s3` restarts this evening. No earlier
  matrix run was looked up to compare.
- Whether a physical S3 card does it. The open shutdown record on
  silicon is cosmetic
  ([garble before the safe screen](2026-11-07-s3-shutdown-garble-before-safe-screen.md)).

## Evidence

`docs/probe/1.0-baseline-2026-11-21-86box/mode-matrix/`: the four matrix
logs. The per-mode reports are in `build/driver-results/mode-matrix-s3-*`
(not committed).

## Next step

Loop restarts on one S3 guest without the `/accel` phase
(`-SkipAccel`), then with it, and count. If it stops again, capture the
COM1 pipe during the shutdown.

## 1.0 re-read, 2026-11-26

Stays open for 1.0 and is named in the README's known issues. Not
reproduced. Named in the README's list of faults seen once and not
reproduced; a recurrence on the release candidate's 86Box runs reopens
the investigation.
