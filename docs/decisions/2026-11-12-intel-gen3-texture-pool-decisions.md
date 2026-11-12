# Intel Gen3 texture pool: the plan's decisions taken, and its errata gate

Date: 2026-11-12
Status: accepted. Decided by Michael Dale with "start on Gen3 then for
memory, lets do it", after the plan's decisions and costs were put to him.
Plan: `docs/plans/intel-gen3-system-memory-textures.md`

## Decisions

1. **Pool size: 32 MiB, fixed**, on a machine with at least 512 MiB of RAM,
   otherwise no pool. No INI key (the 2026-09-18 rule).
   - The pool sits in GMADR directly after the stolen run. The family maps
     16 MiB of GMADR today (`intel_hw16.c`, `0x00ff:0xffff`), so a 32 MiB
     pool needs a 48 MiB map. The HD 4600 maps 64 MiB through the same DPMI
     path, but no Gen3 machine has been asked for more than 16 MiB. Phase 0
     measures it; if 48 MiB is refused, the pool is whatever the mapping
     allows above stolen, at least 4 MiB, or none.
2. **Errata gate: Phases 2 and 3 are authorised on MICHAEL-NETBOOK only**
   (exact device `8086:27AE` revision 03), under the conditions below.
3. **Recovery: `V9X3D OFF` from DOS also turns the pool off.** No verb of
   its own.
4. **The 98 SE runtime's handling of a texture-only second heap** is not a
   decision but a measurement, and stays in Phase 0.

## Why the gate is needed

Erratum 12 in 309220-0132 describes an incorrect internal-buffer flush for
an unpublished sequence of processor and integrated-graphics memory
accesses, implication "system may hang", no fix on A3. Every GPU read so far
has been of stolen memory, which the CPU writes only through the aperture
(`2026-09-14-cpu-writes-to-stolen-memory-need-the-aperture.md`). The pool
makes the GPU read ordinary system RAM pages. The CPU still writes them only
through the aperture, but that is a new memory interleave, and none of the
earlier gates (`2026-09-12-intel-phase4-errata-gate.md`,
`2026-09-15-intel-phase5-errata-gate.md`,
`2026-09-25-intel-2d-blits-errata-gate.md`,
`2026-09-29-intel-gen3-async-errata-gate.md`) covers it.

The Intel Flush Page (`2026-09-12-intel-flush-page-lead.md`; the netbook's
BIOS has placed it at `FED13000`) is not used by this work. It addresses
stale data, not hangs, and is undocumented on this part. If Phase 3 reads
back stale texels, the flush page is the first candidate, and using it
needs its own record.

## Conditions

- MICHAEL-NETBOOK on AC power, restarted with `v9xctl reset`, never
  `reboot` (`netbook-agent-reboot-hang`).
- Phase 2 (map, no GPU) runs before Phase 3 (GPU samples), and Phase 3's
  single-texture probe runs before any sustained or game run.
- Every CPU write to the pool goes through the aperture. After the one-time
  zero fill and `WBINVD`, nothing writes the pool through its write-back
  linear mapping.
- The pool's PTEs are written only above the stolen run, and only after
  `i9xx_pool.c` has accepted the map (no overlap with stolen, GTT, ring or
  status page, nothing above 4 GiB, nothing past the aperture, no page
  twice).

## Stop rule

A hang, a machine that does not come back from `reset`, or a texel read back
wrong in Phase 3 ends the run. The next boot goes back to a build without
the pool, and the failure is recorded before any variant is tried. Two hangs
with the same build stop the work until Michael decides how to continue.

## What acceptance does not establish

It does not establish that erratum 12 cannot trigger, that the 98 SE
runtime accepts the second heap, or that Serious Sam gets faster. Those are
the plan's Phase 0, 3 and 4 questions. The pool does not extend to the GMA
900, Pineview or GM965 until it has run on the netbook; each of them then
gets its own record.
