# The Final Reality flicker: everything known, and what is not

**Status: OPEN. UNCONFIRMED.** A change has been made that targets the best
remaining mechanism, and nothing has yet shown it helps.

**Machines.** MICHAEL-NETBOOK, Intel 945GSE (8086:27AE), LVDS on pipe B,
plane B, 576 active lines in a 672-line frame, no network - results are
carried on a USB stick. A8U4I5, `10.0.1.172`, S3 Trio3D/2X (`5333:8A13`)
on the ViRGE backend.

**Where the detail lives.** This file is the summary. The decision records
of 2026-09-19 carry the captures and the arguments; the long-running
narrative is
`2026-09-18-final-reality-flicker-is-the-buffer-under-construction.md`.

## The symptom, photographed

**Operator clarification, 2026-09-20:** rendering looks correct; the problem
is intermittent flicker, not persistent missing geometry or textures. It
has not been seen on some other GPUs. That observation does not establish
a Final Reality defect: the benchmark could expose a timing or bandwidth
problem in this driver. Intel and S3 share the DirectDraw/D3D core despite
their different presentation backends. A matched run with the stock driver
on the same physical GPU would be a stronger comparison.

One display frame in eight or nine shows only the cleared buffer and the
sky band at the top of the picture, with everything below it black.
Complete frames either side. Mean luminance drops to roughly 40-60% of its
neighbours for that single frame.

It is **bursty**: about 1.1 such frames a second over a whole Robots pass,
against roughly 7 a second in the heavy stretches. It concentrates in parts
of the scene. Nothing found in the flip path predicts that, and it is the
single most diagnostic property the investigation turned up.

It appears on BOTH cards, through present paths that share almost nothing:
Intel uses a hardware flip with an active-video issue window, S3 writes the
VGA start address with no beam timing at all.

`docs\decisions\2026-09-19-trio3d-flicker-frames-4902-4909.png` is the
frames; `2026-09-19-dip-local.py` beside them is how they are counted.

## Measured and closed

Each of these had its own instrument, and each read clean:

| mechanism | evidence |
|---|---|
| Premature flip completion | `/reuse` probe: 120 green paints of the retired buffer at 0/17/34/100 ms, and green reaches the panel in exactly one 0.32 s setup fill across 158 s |
| Buffer ownership | `DrawsIntoPresented=0` over 141,322 batches; present trace alternates cleanly, first draw after every flip aimed at the buffer not just presented |
| Draws racing a pending flip | `VirgeDrawsFlipPending=0`, `DrawsFlipWaited=0`, even after completion was corrected to the end of the retrace |
| Unfinished drawing at the flip | 8,192-read confirmation at all 256 flips broke zero times, while the same run caught 45 false settles elsewhere |
| A leaking flip window | `FlipIssueDeltaMax=1` scanline; the apparent 84-line leak was the desktop-restore flips, which take no window test |

**Caveat on all five.** These are "not reproduced by these probes under
these workloads", not proofs of absence. The `/reuse` result speaks for its
own flipping pattern, not the game's. The idle-bit confirmation uses the
signal whose reliability is in question. And correct destinations plus
completed commands do not establish that the submitted commands produced a
complete frame. A mis-ordered clear or a transient rendering-state problem
remains possible, but neither has been demonstrated. Scene dependence
alone does not distinguish those from timing or bandwidth pressure; the
operator reports otherwise correct rendering.

## Changes tried and measured as not helping

Counted as single-frame dips against neighbouring frames over a matched
span, one run each:

| build | dips/12 s | note |
|---|---|---|
| unfixed baseline | 23 | |
| start address written at the blank-to-active edge | **70** | made it worse; reverted |
| flip completes at the END of the retrace, ViRGE draw guard | 22 | kept on VGA-discipline grounds only |
| idle confirmed across 32 reads | 28 | |
| strict 8,192-read settle at the flip | 41* | removed; never fired |

\* measured on a differently-scened window; the rate-normalised figures are
1.09, 4.24, 1.45, 1.27 and 1.50 a second respectively.

Nothing moved toward zero. One run per build against a bursty fault, so a
modest improvement could hide; a four-times regression was caught, so a
large one would not.

## Standing, unexplained

**The scanout underruns.** PIPESTAT bit 31 on the live pipe, clear at a
measurement boundary and set afterwards, on two separate boots (intel90,
intel91). GMCH parts have no underrun interrupt - only this sticky status -
so two days of flip-path instruments could not have found it. An underrun
starves the scanout part way down a frame, which is the observed shape, and
depends on memory bandwidth, which is the observed burstiness. Self-refresh
is NOT the cause: `FW_BLC_SELF` bit 15 reads clear.

**The clear races a pending flip on the netbook.** 1,001 of 1,030 Blts,
against 0 of 55,791 Locks. A real defect on a path that waits for the
engine and never for the flip. Consequence unknown: which buffer the Blt
targets relative to what is displayed is not measured, because
`DrawsToFront` compares against a register that holds the PENDING value and
is blind in exactly that window.

**The watermark is low for the live plane.** `FW_BLC` reads `0x03060106` -
plane A 6, plane B 6 - at both the panel's 1024x576 and the game's 640x480.
With `DSPARB=0x00001D9C` giving plane A 28 entries and plane B 31, and the
pipe at 54,180 kHz and 16 bpp, i915's arithmetic gives **20 for plane B**,
the one driving the panel, and 26 for plane A, which is idle and gets its
FIFO less the guard. Six against twenty is about a third of the margin.

## The change made, and what it is not

From 2026-09-20 the driver programs `FW_BLC` when the pipe source changes,
using i915's calculation over the pipe's own timing and DSPARB partition.
Only the two watermarks and two burst bits are written; bit 25 and anything
else the BIOS left is preserved, because overwriting a bit whose meaning is
unestablished is not a thing to do to a machine reached on foot.
Self-refresh is left disabled as found. The arithmetic is in
`src\common\i9xx_wm.c`, host-tested in `tests\host\test_i9xx_wm.c` against
this machine's own register, timing and answer.

**This is unconfirmed as a fix.** What is measured is the underrun and the
shortfall. That the underrun is what the camera caught is inference from
the symptom's shape and its burstiness. Whether programming the watermark
changes the dip count has not been tested.

**And the shortfall's provenance is not established.** Identical values at
two modes do NOT show the BIOS never reprograms them: both modes share a
pixel rate, a pixel format and a FIFO partition, so a correct per-mode
calculation would produce the same number twice. What the captures
establish is the value, not where it came from.

## What would settle it

Run the changes separately against matched scenes, with repeated runs where
possible: the burstiness makes a single improved run weak evidence.

1. Program the watermark and re-count the dips against the 23-per-12 s and
   1.09-per-second baselines, on the netbook, with the panel recorded.
   `WmWrites` and `WmWritten` say whether the write happened and took.
2. Render heavily into a disjoint offscreen buffer while a known image
   stays on screen, never flipping. Flicker there implicates bandwidth with
   the flip path removed from the question entirely, and the PIPESTAT
   reading reports in the same run.
3. Save the back buffer immediately before Flip, tagged with a sequence,
   and correlate against the panel recording. A saved image also missing
   the robot points at rendering, state, clears or rejected submissions; a
   complete one against an incomplete displayed frame points at
   presentation.
4. Test a build that waits for flip completion before a clear can touch a
   flip-chain buffer (or returns busy for a non-waiting request). Cover
   accelerated fills as well as CPU fills. Record the clear destination,
   operation and flip sequence so a pending flip is not mistaken for proof
   that the clear touched the displayed buffer. Keep the watermark setting
   identical between this build and its control. This is a proposed
   experiment, not an implemented or measured fix.
5. Compare Final Reality on the same physical GPU with its stock driver,
   matching mode, settings and scene. A clean stock-driver run would point
   toward this driver's behavior; observations on different GPUs cannot
   isolate the driver from the hardware.

## Code review follow-up, 2026-09-20

Reviewed at `dfd9548`. These are code findings, not new hardware evidence;
none establishes the cause of the recorded flicker. Tree checks and host
tests passed. No driver changes or hardware trials were made in this review.

- **Watermark writes depend on diagnostic capacity.** In
  `src/display32/engines/i9xx_scanout.c`, `v9x_i9xx_note_watermarks` returns
  when the four-entry log fills, before programming the watermark. Later
  mode changes receive no update until the log resets. Its source-size-only
  comparison also misses pixel-depth or timing changes at unchanged source
  dimensions. Separate programming from logging and track the calculation's
  inputs, not just the source size.
- **Plane identity is used to choose pipe timing.** The same function
  selects PIPEA/PIPEB source and totals using `v9x_i9xx_scanout_plane`, even
  though `v9x_i9xx_scanout_pipe` supports a plane routed to the other pipe.
  Preserve the resolved pipe separately for timing reads. The documented
  plane-B/pipe-B netbook configuration is unaffected by this mismatch.
- **Accelerated S3 blits bypass the pending-flip counter.** In
  `src/display32/ddhal_core.c`, successful engine fills, depth fills and
  copies return before `v9x_blt_drain`, which samples `blt_flip_pending`.
  A zero therefore does not exclude accelerated clears during a pending
  flip. Sampling belongs before engine dispatch, with the destination and
  operation recorded. The Intel CPU-blit measurements remain relevant.
- **The watermark fallback differs from the cited implementation.**
  `src/common/i9xx_wm.c` permits active-plane watermarks down to 1, and the
  host tests expect that fallback. Linux v4.4's `intel_calculate_wm` applies
  a final minimum of 8, citing burst-size constraints. Reconcile this before
  treating the helper as equivalent across modes. It does not change the
  netbook's computed value of 20. Reference:
  [Linux v4.4 intel_pm.c](https://github.com/torvalds/linux/blob/v4.4/drivers/gpu/drm/i915/intel_pm.c).

The known clear/flip exposure remains in place: `v9x_blt_drain` counts a
pending flip but waits only for rendering/engine work, and Lock likewise
has no flip wait. Exposure is not proof of visible corruption, especially
with the conservative two-tick Intel completion rule; destination tracing
and the isolated clear-wait experiment above would test the consequence.

## A note on the instruments

Six instruments built during this investigation were wrong in a way that
read as a result:

- `DrawsToFront` compares against a register holding the pending value, so
  it is blind for the frame the flicker occupies.
- The present trace recorded draws BEFORE the backend's synchronisation
  wait, so a correctly waiting batch looked like premature reuse.
- It then recorded refused batches as submissions, and consumed the
  first-draw marker so the real one went untraced.
- It then took the backend's return value as a submission signal, which it
  is not in either direction.
- `blt_flip_pending` was sampled in a path Lock does not take, so a zero
  covered the Blts only - and once Lock was sampled the number was not zero.
- The PIPESTAT accumulator was sticky with no baseline and no boundary, so
  a mode change alone would have set it in every capture.

A seventh - the DSPARB field layout - was wrong in a way that read as NO
result, and cost one capture instead of a conclusion. That is the failure
mode to aim for.
