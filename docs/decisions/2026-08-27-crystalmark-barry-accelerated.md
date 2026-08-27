# CrystalMark Retro on BARRY: the accelerated run

Date: 2026-08-27
Status: measured; one run, native-S3 column still outstanding

The **after** column for
[the driver-0.5 baseline](2026-08-27-crystalmark-barry-baseline.md), taken once
the ADVFUNC_CNTL defect was fixed and GDI acceleration was safe to enable on
real silicon. Same machine, same tool, same pinned mode.

## Configuration

| | |
|---|---|
| Host | BARRY, `10.0.1.47:9869`, physical S3 Trio32/64 86C764, 2 MB |
| OS | Windows 98 SE, 4.10 build 2222, Pentium 1c/1t, 32 MB |
| Driver | Velocity9x `gdi-accel-004` + the ADVFUNC_CNTL fix (`1cb84ff`) |
| Acceleration | shipping defaults - `GdiAcceleration=gdi-fill-copy-overlap`, upload off |
| Mode | **800x600x16**, `Stage=enable-ok`, `Surface=pitch=1600 bpp=16 w=800 h=600` |
| Tool | CrystalMark Retro 2.1.0 for 9x, `C:\CMR` |
| Tests | 2D, CPU, Disk. 3D not run, as in the baseline |

## Scores

| Group | Test | 0.5.x baseline | Accelerated | Change |
|---|---|---|---|---|
| **2D (GDI)** | Text | 2 | **3** | +1 |
| | Square | 253 | **275** | **+8.7%** |
| | Circle | 134 | **134** | **0** |
| | Image | 91 | **98** | **+7.7%** |
| CPU *(control)* | Single | 99 | 99 | 0 |
| | Multi | 99 | 99 | 0 |
| Disk *(control)* | Sequential read | 44 | 45 | +1 |
| | Random read | 2 | 2 | 0 |
| | Sequential write | 38 | 41 | +7.9% |
| | Random write | 2 | 2 | 0 |

Screenshots:
[2D group](../images/crystalmark-barry-trio64-gdiaccel-800x600x16.png),
[all scores](../images/crystalmark-barry-trio64-gdiaccel-all-scores.png).

## Against the baseline's prediction

The baseline committed to a prediction so this run could be judged rather than
admired: *Square should rise, Image may rise, Circle and Text should not move,
and CPU and Disk must not move.*

- **Square rose**, 253 to 275. Solid rectangle fills are exactly what build 001
  turned over to the engine.
- **Image rose**, 91 to 98, so enough of that test's blits are
  screen-to-screen to reach builds 002/003. A memory source still declines.
- **Circle did not move at all** - 134 to 134, bit-identical. Ellipses go
  through `Output`, which is not accelerated.
- **CPU did not move** - 99/99, bit-identical.
- **Text moved by 1**, from 2 to 3, where the prediction said it should not
  move. See below; this is not a claim of improvement.

## Reading these numbers honestly

**The strongest evidence here is not the size of the gains, it is Circle and CPU
being bit-identical.** Three scores that should not move did not move by a single
point, which is what makes the two that did move attributable.

**But one control did move, and it undercuts the margins.** Sequential disk write
went 38 to 41, +7.9% - almost exactly the size of Square's +8.7%. Nothing in a
display driver writes to disk, so that is run-to-run variance, and it means this
machine can produce an 8% swing on a score for reasons unrelated to the change
being measured. Disk is noisier than compute by nature - seek and cache
behaviour - so this does not automatically transfer to the 2D column, and the
bit-identical Circle and CPU argue those scores are low-variance. It is still the
reason not to quote "+8.7% on Square" as though it were a measured speedup.

**Text 2 to 3 should not be read as an improvement.** Glyphs arrive through
`ExtTextOut` and `StrBlt`, ordinals this driver still forwards to the DIB Engine
untouched, so there is no mechanism for acceleration to have moved it. A one-point
change on a two-point score is quantisation, and the honest description is that
Text remains pinned near the floor - which is where the real 2D headroom on this
driver still is, and it is a separate piece of work.

**This is one run of each column.** A repeat of both, ideally interleaved, is what
would turn these into numbers worth defending.

## The driver was verified live for the whole run

A benchmark against a self-disabled driver would look like the baseline and pass
unnoticed - the exact failure mode that wasted time earlier in this defect. So
the state was checked on both sides of the run:

- Before: `GdiAcceleration=gdi-fill-copy-overlap`, `Stage=enable-ok`.
- After: still `gdi-fill-copy-overlap`, **not** poisoned.
- `/probe` immediately after the run: `ProbeChangedPixels=3072` of 3072, bounding
  box exactly the requested (64,48) 96x32 rectangle. Fills land correctly on
  physical silicon after the workload.
- `Fills=5532` over the session, `Poisoned=0`.
- **`AdvFuncRestores=5`** - the ADVFUNC_CNTL guard fired five times. Something in
  ordinary operation cleared bit 0 five times and the driver put it back each
  time. That is the fix earning its place against a real workload rather than a
  synthetic poison.

One note on the machine's reported adapter name: CrystalMark shows
**"S3 Trio32/64 PCI (732/764)"** where the baseline recorded
"Velocity9x S3 Trio32/64 86C764". That is the `[boot.description]` string in
`SYSTEM.INI`, which was restored from an audit capture during this defect's
recovery. Cosmetic, and not a driver change.

## An unresolved display artifact, recorded rather than explained

A full-screen screenshot taken after the run shows the CrystalMark window
duplicated at several scales with large black regions
([capture](../images/crystalmark-barry-trio64-post-run-display-artifact.png)).

This is **not** attributed, and should not be read as the accelerated driver
corrupting the display, because the evidence points the other way: the `/probe`
run taken moments later - which snapshots the whole screen through the same
BitBlt plus `GetDIBits` technique - returned a pixel-exact result, and
`/accel` passes. A driver mis-addressing its fills cannot produce an exact 3072
of 3072 bounding box.

The likelier candidates are the agent's screenshot path (a documented artifact of
that pipeline already exists at 640x480 -
[issue](../issues/2026-08-27-getdibits-fullscreen-doubled-at-640-barry.md)) or
genuine leftover desktop damage from CrystalMark's own window teardown that the
probe then painted over. Distinguishing them needs someone looking at the
physical CRT, which is the one instrument this session does not have.

Flagged here so the next physical session checks it deliberately.

## Outstanding

1. **The native S3 driver on the same machine, same mode** - the third column,
   still not taken. Without it these numbers say what acceleration changed but
   not how far from the vendor driver the result sits.
2. A repeat of this run and the baseline, for the variance reason above.
3. Confirmation of the display artifact above, by eye, on the physical screen.
