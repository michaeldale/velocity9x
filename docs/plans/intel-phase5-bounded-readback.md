# Phase 5 read-back, now that bulk aperture reads hang the part

Decided 2026-09-15 and implemented; see **Decided** and
**Implemented** below. The options are kept because the decision refers to
them and because the ones not taken may be taken later.

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
the draw.

**There is no measured safe interval.** An earlier draft of this file said the
threshold lay "somewhere between 64 and 153,600"; that was wrong, and wrong in
a way worth naming. The 64 successful reads were of the *scratch*, at a
different address range, through a different read path and a different
implementation. The hang was a different range, a different path, and its exact
failure point inside the hash is unknown - the capture proves only that the
operation as a whole did not return. Address coverage, implementation, pacing
and cumulative count are all confounded, so those two figures do not bracket
anything.

Fourteen probes is therefore a **conservative starting point, not a proven-safe
figure.**

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

## Options considered

### A. Bound by sampling, keep the structure

Row CRCs at a stride, and a hash over one page instead of the whole target. At
stride 16: 30 rows x 320 = 9,600 reads, plus ~1,024 for a one-page hash, plus
14 probes - about **10,600**. Keeps localisation at 16-scanline resolution, but
the stride would be a guess with a machine lock as the failure mode.

### B. Pixel probes only

14 reads, plus the 3 already completed. About **17**. Same order as the Phase 4
scratch verify, though that is not the same read path. A failed draw then says
only that the probes are wrong, with nothing to localise it.

### C. Let the GPU do the reduction

Add a blit to the stream that copies the target into the existing 4 KiB
scratch, then read only the scratch - at most 1,024 reads. Bounded by
construction, but the reduction becomes part of the armed stream and needs its
own decoder entry, CRC and errata assessment; and a blit that misbehaved would
produce a plausible-looking scratch.

### D. Characterise the access pattern

Batches with committed `Started`/`Completed` markers carrying address range,
read count, cumulative count and pacing, stopping at a diagnostic budget.

### E. Pace the reads

Same totals with a stall every N dwords, to separate read rate from read
volume. Erratum 12 describes an access *sequence*, which hints at rate, but
that is reading intent into a one-line summary.

## Decided

Michael Dale, 2026-09-15, revising an earlier draft of this file that
overstated what the measurements established.

**B first, then a revised D as a separate diagnostic.** D -> A is explicitly
not the default.

1. **The 14 named probes carry the first armed draw.** The full-target hash and
   the row CRCs come off that path. Execution evidence is committed before any
   pixel is read, and each probe result is committed as it completes, so a lock
   part way through cannot cost the whole set.
2. **Unarmed B1 gets a small explicit sample set** in place of the bulk hash.
   Mapping, backing and bounds are validated before any aperture access, on
   every path, independently of whether the run is armed. Reading a point twice
   is reported as *sample stability*; it cannot establish stability of the
   target.
3. **D stays optional, and measures access patterns rather than a threshold.**
   Each batch records a committed `Started` and `Completed` marker carrying its
   address range, read count, cumulative count and pacing. It stops at a useful
   diagnostic budget instead of deliberately seeking a lock.
4. **E is not free inside D.** Once an unpaced rung hard locks, the paced
   comparison cannot run in that boot; the two need separate boots. And the
   committed markers between batches themselves change the pacing, which the
   experiment has to account for rather than ignore.

**C is deferred** until there is evidence the extra GPU operation is needed. It
adds an operation that itself needs validating, and Phase 4's 64 successful
scratch reads do not establish that 1,024 are safe.

**Cheap extra diagnosis, if the initial probes succeed:** a few more pixels
chosen around the expected edges, inside an explicit total read budget. That
can separate displacement from missing coverage without paying 320 reads per
sampled row.

Sequence: bounded probes -> first triangle evidence -> controlled read-back
experiments -> richer diagnostics if justified.

## Implemented

`intel_3d16.c`, alongside this revision:

- `v9x_p5_validate_mapping` runs before the first aperture access on every
  path. It used to sit behind the preflight's `armed` test, so B1 read the
  aperture with none of it checked.
- `v9x_p5_sample_target`: eight points, each read twice, 16 reads, each pair
  committed. Keys `SA*`/`SB*`/`SampleStable`, named for sample stability.
- The bulk hash function is deleted rather than left unreferenced. Steps 27 and
  28 are retired and their numbers reserved, so an old capture's `IntentStep`
  cannot be misread as a new step.
- Probes commit either side of each read.
- `HashOmitted` and `RowCrcOmitted` state the omission in the capture.

`check-intel-3d-capture.ps1`, so omissions are reported and absent evidence
cannot pass:

- Unarmed captures must declare `HashOmitted`, report `SampleCount`, carry
  every `SA`/`SB` pair, and agree pairwise. A truncated sample set fails.
- `SampleReads` is capped at 256 - a guard rail against a future change walking
  the count back toward bulk scale, explicitly **not** a measured safe limit.
- Armed captures must declare both omissions, must not carry `FillHashA`,
  `DrawHashA` or `R0000`, must report `PixelProbes`, and must carry every probe
  it claims. Previously the reference comparison skipped absent keys, so a
  capture with no probes at all reported no mismatches.

Twelve mutations rejected, including a missing sample pair, an undeclared
omission, a zero sample count and a read count back at bulk scale.

## Not in scope


Reducing what Phase 5 proves is a separate question from how it reads memory
back. The triangle, the stream, the CRCs and the arm contract are unchanged by
any option here.
