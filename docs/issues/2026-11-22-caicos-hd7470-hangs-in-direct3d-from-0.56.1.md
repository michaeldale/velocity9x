# HD 7470 (Caicos): the first Direct3D program hangs the machine from 0.56.1; 0.56.0 runs

Date: 2026-11-22. Status: cause isolated to the per-group Evergreen
flush of c0c9381b (0.56.1) and fixed in source by closing the group
after every batch on the non-Trinity parts; V9XD32 passes on the card
with the fix. Not released. The HD 5450, HD 5570 and HD 5770 not run.

## Machine

M-D7TFW4 (remote help): AMD A10-5700, an HD 7470 Caicos XT
(`1002:6778`) fitted beside the APU's HD 7660D (`1002:9901`), both
present. The driver binds the HD 7470 as the display
(`V9XHW.INI`: DeviceId 6778, `Direct3D=hardware-evergreen-cp`,
EngineCaps 01000017, `DpmiMap=08000000:ok`), 1024x768x32.

## Background

The 0.57.0 release checks (2026-11-19) failed on this machine with the
HD 7470 fitted: V9XD32 T13, T4, T16 and T14, hangs, the agent dropping
off. Released 0.56.2 failed the same way, so the 0.57.0 record called it
no regression and left the card question open. The 1.0 baseline of
2026-11-21 did not run the machine.

## What was measured

Today's probes (`ac9aa76b-dirty`) against released `ati` packages,
installed by `run-release-checks.ps1` with a `PackageDir` per entry
(WININIT renames, `reset`, every installed file's hash checked):

| Package | Boot | Result |
|---|---|---|
| 0.56.0 | 97 to 98 | 8 of 8 installed. V9XD32 at 32 and 16 bpp: `Failures=1`, T22X only (the XRGB8888 texture draws black, fixed in 0.56.1 by 1993c5eb). V9XGLP, V9XGLIDP, V9XR3DP, V9XWPRS, V9XFBCMP pass. Engine timeouts and resets 0. No hang |
| 0.56.1 | 98 to 99 | 8 of 8 installed. The machine stopped answering during the first V9XD32 run and has not checked in since |

So the HD 7470 draws correctly on 0.56.0 with the second Radeon
present, and the two-card state is not the cause.

## Read before the runs, from the 0.56.2-era install

`C:\V9XDIAG\V9XEGCP.TXT`, undated, the record the driver writes when it
gives the command processor up: `CP_RB_RPTR=740h` against the driver's
`Wptr=680h`, and among the logged puts `PutRptrSeen=848h` with
`PutWptrAfter=830h`. The register reads ahead of the write pointer, as
the Oland's CP_RB0_RPTR did before 1101edcb moved GCN to the writeback
copy. `eg_cp.c` still takes the pointer from the register
(`v9x_eg_cp_pending`).

## What 0.56.1 changed for this card

- c0c9381b: Evergreen batches flushed per group of 16 and
  `batch_triangles` 512. Its message says "Cedar and Caicos not run".
- 1993c5eb: XRGB8888 textures on the Evergreen class.
- d3c15653: Trinity's engine clock. Gated on `EG_TRINITY` and on a ROM
  whose device is 9901, so by reading it does not run on this card;
  `V9XCLK.TXT` on the machine shows a 200 to 304 MHz set, which may date
  from the HD 7660D boots.

## The group flush, measured

The machine came back by itself about 13 minutes after it stopped
(boot 100, nobody asked). Then `egclose1-dirty`: the current tree
(`947d7d3d`) with one change in `eg_cp.c`, every Evergreen part that is
not Trinity closes its flush group after each batch, as 0.56.0 did.
`batch_triangles` stays 512 and XRGB8888 textures stay offered.

| Package | Boot | Result |
|---|---|---|
| `egclose1-dirty` | 100 to 101 | 8 of 8 installed. V9XD32 PASS at 32 and 16 bpp, `Failures=0`. V9XGLP, V9XGLIDP, V9XR3DP, V9XWPRS, V9XFBCMP pass. No hang |

So the per-group flush of c0c9381b is what hangs the HD 7470; the
larger batch and the XRGB8888 change are cleared. The unchanged current
tree was not run on the card: 0.56.1, 0.56.2 and the 0.57.0 checks are
its evidence, and one more run is one more hang.

## Not known

- Why a group hangs Caicos and not Trinity. The register reading past
  wptr in the older fault record is a lead, not a finding: with groups,
  up to 16 batches sit in the ring without a pipe-wide wait.
- Whether the fix costs Caicos anything; no benchmark was run. On
  Trinity the groups were worth 3821 to 4130 in 3DMark 2000.
- Whether the HD 5450, HD 5570 and HD 5770, on the same engine code,
  hang the same way on 0.56.1 to 0.60.0. None has been run since
  0.34.0.

## Next step

Ship the per-batch close for Cedar, Redwood, Juniper and Caicos. To get
the groups back on them: read the ring pointer from writeback, as GCN
does since 1101edcb, and run a group build on this card with the owner
told first. Run one of the HD 5450, HD 5570 and HD 5770 on the fixed
build.

## Evidence

`docs/probe/caicos-0.56-ab-2026-11-22/`.
