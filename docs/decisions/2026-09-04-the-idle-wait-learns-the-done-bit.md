# The idle wait learns whether the part has a 3D-done bit

Date: 2026-09-04
Status: measured on A8U4I5 and on the emulated ViRGE/DX; shipped

## The tax

`v9x_wait_idle` requires SUBSYS_STAT bit 1 after a triangle is launched,
because 86Box's ViRGE hands the triangle to a render thread and its idle bit
reads true in the gap before that thread wakes
(`2026-09-03-the-probe-matrix-and-the-3d-done-bit.md`). When the bit does not
come the wait spins to `V9X_VIRGE_DONE_SPIN_LIMIT` - 4,096 status reads - and
then accepts idle anyway, without a reset.

The Trio3D/2X never sets the bit. The first matrix run on that card wrote a
`_Dmiss` delta beside all 117 cells and both render targets
(`2026-09-04-the-trio3d-runs-the-matrix.md`), so the full spin was being paid
on every wait, on every triangle, for a bit that was never going to arrive.

## The rule

Policy, in `src/common/donewait.c` behind `include/velocity9x/donewait.h`, with
no I/O in it and `tests/host/test_donewait.c` holding it to a table - the same
split as `src/common/mtrr.c`. The engine reports what each wait saw and asks
whether the next one should spin.

The rule runs one way on purpose. A part that has produced the bit even once is
never given up on: one sighting proves the bit is real there, and a later miss
is exactly the gap the spin was written to cover. Only a part that has missed
`V9X_DONE_WAIT_GIVE_UP` waits in a row, having never once answered, is decided
against - and a `v9x_done_wait_reset` puts the question back. 64 is the limit;
the emulated ViRGE answered every one of 332 waits in a probe run, so 64
consecutive misses with nothing ever seen is not bad luck.

`done_skipped` is appended to `V9X_D3D_DIAGNOSTICS` and reported by V9XTRACE as
`D3dDoneSkipped`, so a run says whether the rule fired. `done_skipped` rising
with `done_seen` at zero is a part without the bit; `done_skipped` non-zero on
a part whose `done_seen` is also non-zero would be the rule firing when it
should not, and is what to look for on any new chip.

## Measured

A8U4I5, one probe run, boot 26:

```
D3dDoneSeen=0   D3dDoneMissing=64   D3dDoneSkipped=268
TexMatrixMs   840 -> 435            TargetsMs 50 -> 50
```

64 misses is the limit exactly; every wait after it was skipped. The matrix
block halves. Against the same card's run an hour earlier, `compare-probe.ps1`
reports **one** differing key in the whole file - `AlphaCurve_Dmiss`, 30 to 25 -
and 120 `_Dmiss` keys that are now absent because nothing missed. Not one pixel
changed. That is the correctness claim: giving up the bit on this part alters
nothing it draws.

Emulated ViRGE/DX, same binaries, boot 554:

```
D3dDoneSeen=332   D3dDoneMissing=0   D3dDoneSkipped=0
```

The rule does not fire where the bit works, and the probe file is byte-identical
to the run before the change - `0 unexpected, 0 expected, 0 only-left,
0 only-right`.

## Gates

check-tree, vga survey safety gate, host tests and family packages (run-checks).
The give-up rule was watched failing first: `v9x_done_wait_should_spin` stubbed
to always spin makes `test_donewait.c` fail, and the test suite is reached from
`test_main.c`.

## What this does not do

- It does not make the Trio3D's 3D-done bit work, and does not say whether the
  part has one somewhere else. It stops paying for the one it does not have.
- It does not touch the idle bit's own spin, timeout or CR66 reset. An engine
  that says it is working is still waited for and still recovered.
