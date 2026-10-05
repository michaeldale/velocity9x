# SiS 6326: a mip filter set for magnification takes its in-level half

Date: 2026-10-05. Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), boots 290
and 291. Evidence: `docs/probe/a8u4i5-sis6326-fr-stall-2026-10-05/fold-b291/`.
Follows [TEND after each triangle](2026-10-05-sis6326-tend-after-each-triangle.md).

## Measured

- **Why Final Reality's batches were refused.** The SiS engine now counts
  its mapping refusals by reason, in the Mach64 policy counters the Rage
  IIC already uses. Final Reality's full run refused 17,192 batches, every
  one for reason 13, the texture filter (boot 290). The mapping accepted
  only NEAREST and LINEAR as the magnification filter.
- **With the fold (boot 291)** Final Reality's full run refused nothing
  and timed nothing out.
- **V9XDDP on the same build** compared identically to the boot 231 run.
  It still fails no check that SiS's HAL passes.

## Decision

A mip filter set as the magnification filter maps to its within-level
half:

- MIPLINEAR and LINEARMIPLINEAR give bilinear;
- MIPNEAREST and LINEARMIPNEAREST give nearest.

Magnification has no levels to choose between. `d3d_i9xx.c` already folds
it this way. Host-tested.

## Not established

- Which mag filter value Final Reality sets. The counter records the
  reason, not the value. That the fold cleared all 17,192 refusals says it
  is one of the four.
- Whether every scene looks as under SiS's HAL. Screenshots during page
  flips read the hidden page, so only some frames were seen.
