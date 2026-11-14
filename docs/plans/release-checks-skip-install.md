# Release checks: rerun one machine without installing again

Date: 2026-11-14. Status: **proposed; nothing coded.**

## Why

`scripts/run-release-checks.ps1` has two modes. The default installs each
family package by WININIT rename and a `reset`, checks every installed
file's hash, then runs the probes and 3DMark. `-Baseline` skips the
install and the hash check, but it is for the drivers *as found*: it
writes `<name>-before` folders and a separate `-baseline` record, and the
summary labels the machine "as found".

Nothing reruns the checks on a machine that already has the release
package installed. A check that fails for a reason outside the driver
then costs a second install and a second reset, and on a slow or remote
machine that is the expensive part:

- The 0.50.0 checks (2026-11-14): the GM965 (`M-KYHT4Q`, TOMMOHP)
  installed 8 of 8 and passed the hash check, then Windows' "Enter
  Network Password" dialog came up over 3DMark (`3dmark-stuck.png`), and
  the worker stopped with no probe run. The driver was installed as
  built; only the probes and 3DMark were owed. The machine's notes say
  this dialog follows every reset.
- The i5-4570 takes over five minutes to boot; a relay machine adds
  30-80 s a call.

## Proposal

A `-SkipInstall` switch, for the machines named with `-Machine`:

1. No staging, no WININIT.INI, no `reset`.
2. The hash check still runs (`Test-Installed` for every file in the
   family's installed set). Any mismatch fails the worker with "The
   package is not installed as built", as an install would, so the
   record can only report a machine that runs this build.
3. The worker writes its normal folder and log (`<name>`, not
   `<name>-before`), replacing that machine's earlier folder in the same
   `-OutDir`, and the record's install column says "installed earlier,
   hashes checked" with the boot counter.
4. The record merges: rerunning one machine into an existing run's
   `-OutDir` rewrites the record from every machine folder there, as
   `-SummaryOnly` does today.

## The logon dialog itself

Separate, and worth doing with it: after the reset, the runner waits for
the desktop (`wait-desktop`), but the GM965's dialog appeared later,
during 3DMark. Agent 0.17.0 has `wait-desktop -DismissLogon`, which
cancels a recognised Win9x startup logon (`Enter Network Password`,
`Enter Windows Password`). The GM965 runs agent 0.16.0. Options: update
that agent (Michael's call, remote help), or have the runner close a
window titled `Enter Network Password` before 3DMark and before the
probes (`close-window`, agent 0.16.0+).

## Done when

The GM965 case above reruns with `-Machine gm965 -SkipInstall` in one
call, no reset, and the record shows it beside the other machines of the
same run.
