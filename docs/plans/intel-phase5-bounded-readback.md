# Phase 5 read-back, now that bulk aperture reads hang the part

Open decision. Nothing here is implemented.

Depends on `decisions/2026-09-15-bulk-aperture-reads-hang-the-945gse.md`: the
614,400-byte two-pass hash of the render target hard locks MICHAEL-NETBOOK,
while three individual aperture reads in the same run completed.

## The numbers

Target is 640x480 at a 1280-byte pitch, `0x96000` = 614,400 bytes.

| Read-back | Aperture reads | Status |
|---|---|---|
| Heap probe + two guards | 3 | Completed on hardware |
| Phase 4 scratch verify (8x8 at pitch 32) | 64 | Completed, 2026-09-14 |
| 14 named pixel probes | 14 | Never reached |
| 480 row CRCs, 320 reads each | 153,600 | Never reached |
| Full-target hash, two passes | 307,200 | **Hangs** |

So the armed path as written performs about **460,800** aperture reads after
the draw. The safe threshold is somewhere in the open interval between 64 and
153,600 and nothing has measured it.

Note what that means for the armed boot that has not happened yet: it would
have drawn the triangle and then hung reading the result back, losing the
evidence the boot was spent on.

## What each read-back is for

- **Pixel probes (14)** - is the triangle where the rasteriser says it should
  be? Fourteen named points, each recorded inside/outside. This is the actual
  pass/fail evidence for "did it draw a triangle".
- **Row CRCs (480)** - when the picture is wrong, *where* is it wrong? A whole-
  target hash can only say that something differs; per-row CRCs localise it to
  a scanline. This is the debugging instrument, not the verdict.
- **Full-target hash** - two passes over the same bytes, compared, to show the
  mapping is stable rather than returning noise. On the unarmed boot it is the
  only thing B1 proves about the target.

## Options

### A. Bound by sampling, keep the structure

Row CRCs at a stride, and a hash over one page instead of the whole target.

At stride 16: 30 rows x 320 = 9,600 reads, plus 1,024 for a one-page hash,
plus 14 probes. About **10,600**.

- Keeps localisation at 16-scanline resolution, which is enough to tell a
  misplaced triangle from a torn one.
- Still an order of magnitude above anything measured as safe. Picking 16
  rather than 64 or 4 would be a guess with a machine-lock as the failure mode.

### B. Pixel probes only

14 reads, plus the 3 already proven. About **17**.

- Almost certainly safe: the same order as the Phase 4 scratch verify.
- A failed draw then says only "the probes are wrong", with nothing to localise
  it. Every subsequent diagnosis costs another armed boot, and armed boots are
  the expensive thing.

### C. Let the GPU do the reduction

Add a `XY_*_BLT` to the stream that copies or downscales the target into the
existing 4 KiB scratch, then have the CPU read only the scratch - 1,024 reads
at most, and the scratch read-back path is already proven by Phase 4.

- Bounded by construction, and the CPU never touches the target.
- The reduction is then part of the armed command stream, so it needs its own
  decoder allowlist entry, its own CRC, and a new errata-gate assessment. It
  also cannot prove the target directly: a blit that itself misbehaved would
  produce a plausible-looking scratch.

### D. Measure the threshold first

One unarmed boot with a staircase: read 64 dwords, commit a marker, 128, 256,
512, ... doubling, with a committed `B1Reads=<n>` before each step. The machine
locks at some rung and the capture names the last one that completed.

- Turns the bound from a guess into a number, on the boot that is cheapest to
  lose - unarmed, no token, no writes.
- Costs one boot, and gives a threshold for *this* machine on *this* day. It
  may depend on what else is contending for the aperture, so it wants margin
  rather than being treated as exact.

### E. Pace the reads

Same totals as today, but with a short stall every N dwords, to test whether
the fault tracks read *rate* or read *volume*.

- If it is rate, the full evidence set survives with a delay loop.
- If it is volume, this spends a boot and changes nothing. Erratum 12 describes
  an access *sequence*, which leans toward rate, but that is reading intent
  into a one-line erratum summary.

## Recommendation

**D, then A sized from what D returns.**

D is the only option that replaces a guess with a measurement, it costs the
cheapest possible boot, and every other option is improved by knowing the
number. A is then sized with real margin instead of a stride picked because it
looked reasonable.

B is the fallback if D shows the threshold is low - under a few hundred - since
at that point localisation is not affordable at all and C becomes the only way
to keep it.

E is worth folding into D for free: make the staircase's later rungs paced and
see whether a paced rung survives where an unpaced one of the same size did
not. That answers rate-versus-volume in the same boot.

## Not in scope

Reducing what Phase 5 proves is a separate question from how it reads memory
back. The triangle, the stream, the CRCs and the arm contract are unchanged by
any option here.
