# What ends a DrawPrimitives2 call, and what DDI 6 actually cost Half-Life

Date: 2026-10-06
Status: accepted

## Context

At DDI 6 (`Direct3DDdi=6`) Half-Life ran 30 % slower on the Rage XL and 35 %
slower on the netbook than at DDI 5, while the driver did less work than at
DDI 5. The working explanation (`docs/plans/ddi6-drawprimitives2.md`, Part C)
was that the runtime turns the game's ~0.94M DX5-style draws into ~3.09M
DrawPrimitives2 calls, "about three callbacks per draw", and that the cost
was the runtime's translation. That number came from whole-session counters.

Measured with a per-call capture added for this (`src/display32/d3d/
d3d_dp2_ring.h`, probe bit 32, read by `tools/diag/dp2ring.py`) and a DX5/DX6
reproducer (`tools/diag/dp2_repro_win32.c`), on A8U4I5 (Rage XL PCI,
DirectX 9.0c, `d3dim700.dll`) and the netbook (GMA 950). Evidence in
`docs/probe/a8u4i5-rage-xl-pci-2026-10-06/ddi6-runtime-flush/`.

## Options

The hypotheses, and what the evidence did to each:

1. **One legacy draw becomes ~3.3 DrawPrimitives2 calls.** Killed. At DDI 5
   the 0.84M HAL calls carried 3.86M draw records (`DpRecords`), about 4.6
   draws per call; at DDI 6 the same draws arrive about one per call. The
   runtime batches less, it does not multiply. And the whole-session totals
   are dominated by the console screen between demos (363 to 526 one-quad
   calls a frame at 50 to 63 fps, `xl-console-capture.txt`), not by the
   demo: inside the timedemo there are 130 to 210 calls a frame.
2. **The per-call cost (runtime translation plus HAL entry) is the
   regression.** Killed as the main cause. In the demo a frame on the Rage
   XL was 81 M cycles, of which ~25.5 M was one `Clear2` (Z only, one
   rectangle) done by the CPU across PCI. DDI 5 clears Z with a
   `DDBLT_DEPTHFILL` Blt on the engine. That clear was the whole gap
   (DDI 5 frame 56 ms, DDI 6 frame 80 ms).
3. **State changes, texture changes or vertex conversion end the batch.**
   Killed by the reproducer: 200 draws with alternating textures, alternating
   render states, Half-Life's alpha-test toggle, or the same texture set
   again, each arrive as one call (`repro-device2-phases.txt`).
4. **The runtime ends a batch where it does not own the vertices.**
   Confirmed, two ways:
   - an indexed draw whose vertex array has more than 16 vertices (24, 32,
     48 and 64 measured; 8, 12 and 16 batch) is handed over as
     `D3DHALDP2_USERMEMVERTICES` (`dwFlags 0x9`) and sent at once, one call
     per draw;
   - a draw from a vertex buffer (`DrawIndexedPrimitiveVB`) references the
     application's buffer in place (`dwFlags 0x8`, the buffer's full size as
     `dwVertexLength`, offset 0), and any `Lock` of that buffer ends the
     batch, `DDLOCK_NOOVERWRITE` included. 200 such draws without locks are
     one call; with a lock before each, 200 calls
     (`repro-device3-phases.txt`). Half-Life's calls have exactly this
     signature on both machines: one 32,768-vertex buffer, `dwFlags 0x8`,
     offset 0, one or two indexed lists per call.
5. **A driver cap steers the runtime away from handing over application
   memory.** Tested for `D3DDEVCAPS_TLVERTEXSYSTEMMEMORY` (probe bit 64):
   the runtime reported the cap gone (`HalDevCaps 0x2611`) and still passed
   user memory and still flushed per draw (`repro-device2-phases-no-
   tlvsysmem.txt`). Killed for that cap. No other cap was tried.

## Decision

- `Clear2` clears Z through the `DDBLT_DEPTHFILL` path
  (`v9x_hal_depth_fill`), the engine's fill, falling back to the CPU only
  for what the engine refuses; the colour part stays on the CPU. Same
  build, same boot, Half-Life `timedemo mwd5` at 640x480:

  | Machine | DDI 5 | DDI 6 before | DDI 6 now |
  |---|---|---|---|
  | Rage XL (A8U4I5) | 17.99, 17.98 | 12.57, 12.58 | 17.05, 17.53 |
  | Netbook (GMA 950) | 42.24, 42.00 | 27.2 (earlier build) | 40.82, 42.25 |

  Pictures correct on both (`*-frame.png`); every `Clear2` went to the
  engine (`Dp2Clear2EngineDepth` 1,164 of 1,164).
- The remaining per-draw batch ending is the runtime's rule for memory it
  does not own, decided before the driver is called. It is not reduced from
  the driver, and is not worth patching the runtime for: after the clear
  fix it is ~2.5 % on the Rage XL (206 calls a frame at ~3,600 cycles in
  the driver plus the runtime's share) and nothing measurable on the
  netbook.
- The binary trace of the runtime's flush path (Ghidra/IDA) was not done:
  the reproducer settled which conditions end a batch, and the cost left
  does not justify it.

## Consequences

- DDI 6 is no longer a regression for Half-Life on either machine, which
  changes Part B of the plan: a per-application opt-in is less urgent, and
  whether DDI 6 could become the default needs other DX5/DX6 titles
  measured first, not this one.
- Any game that locks a vertex buffer per draw, or draws indexed from large
  arrays, will reach the driver one draw per call at DDI 6. The driver's
  own cross-call batching (`v9x_d3d_dp2_run`) already absorbs the engine
  side of that; the per-call cost is the runtime's.
- Found on the way: on Windows 98 the HAL DLL's data is shared by every
  DirectDraw process (the capture's state survived from one Half-Life run to
  the next). Per-process pointers kept in HAL statics, such as the
  DrawPrimitives2 probe cache, are only safe because context creation
  forgets them; two Direct3D processes at once have not been tested.
- Instruments added, temporary: the capture (probe bit 32, `Dp2RingSkip`,
  `Dp2RingDelayMs`), probe bit 64, the reproducer. Remove with the probe
  bits.
