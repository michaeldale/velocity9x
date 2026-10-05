# SiS 6326: TEND after every triangle stops Final Reality's stall

Date: 2026-10-05. Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), boots
284-289. Evidence: `docs/probe/a8u4i5-sis6326-fr-stall-2026-10-05/`.
Resolves [the Final Reality stall](../issues/2026-10-05-a8u4i5-sis-3d-stalls-in-final-reality.md).
Plan: [sis-6326-hardware-3d.md](../plans/sis-6326-hardware-3d.md), phase 5.

## The finding

The driver now writes TEND, the byte register at 8AFFh, after each
triangle's vertex registers. The datasheet calls TEND "a dummy byte register
marking the end of a primitive list" (DS L5772-5777) and does not say when
it is required. Until now the driver never wrote it, and neither did the
probe phases.

## Measured

- **Replays (SIS3D `/phase6 /file`).**
  - Boot 285's batch with TEND after each triangle drew all 64 triangles
    (boot 287). The same batch without TEND stalls at triangle 0, with
    Final Reality's real texture loaded (boot 286) or a filled one (boot
    232).
  - Boot 258's batch drew all 64 with TEND (boot 287); without, it stalls
    at triangle 11 (boot 259).
  - Triangle 11 alone went idle with TEND (boot 288). Without TEND it never
    went idle in 120 further waits (boot 264).
- **Final Reality, full benchmark (boot 289),** on the driver writing TEND
  after every triangle:
  - every scene rendered;
  - `EngineIdleTimeouts=0`, `FlipDeclined=0`;
  - 290,226 primitive calls.

  Reported scores, recorded and not compared: 3D 1.85, overall 3.55.
- **Changes that did not stop the stall, before TEND** (issue, boots
  232-283):
  - SiS 2.28's values for every register it writes and the driver does not;
  - its sequencer setup, the Turbo Queue included;
  - RGB555 texels;
  - Z buffer contents (7FFFh or FFFFh);
  - vertex offsets;
  - texture and Z placement.

  Only Z off, or a Z buffer that every pixel fails, let the batch finish.

## What this settles

1. **A Z-tested, textured triangle on this chip can hang the 3D engine
   unless TEND follows it.** Which triangles hang depends on state and
   geometry in ways not understood. For that reason TEND goes after every
   triangle rather than under a condition.
2. **The cause in the hardware is not established.** SiS's HAL presumably
   writes TEND too, since it draws the same scene. Its write sequence was
   not captured: the register snapshots show state, not order.

## Not established

- Whether once per batch would do; it was only measured per triangle.
- What TEND costs. The 3D score above is one run of a benchmark that does
  not isolate it.
- Why the mapping refused 17,013 of Final Reality's batches. Those draws
  were dropped, not drawn, and the SiS engine does not count reasons yet.
  A separate gap.
- V9XDDP was not rerun on this build.
