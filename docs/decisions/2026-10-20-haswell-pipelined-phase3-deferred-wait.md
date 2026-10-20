# HD 4600: draws left running, one at a time; 3DMark 2000's batch drops from 157 to 127 µs, and Half-Life gets none of it

Date: 2026-10-20. Machine: the i5-4570, boots 39 to 41, build
`f0051c73-dirty` (driver, HAL and trace agree), installed by WININIT
rename. Phase 3 of `docs/plans/intel-haswell-pipelined-submission.md`.
Evidence:
[`../probe/hsw-4570-pipelined-phase3-2026-10-20/`](../probe/hsw-4570-pipelined-phase3-2026-10-20/).

## The change

`d3d_hsw.c` applies `hsw_pipe.c`:

- **Two slots.** The state, batch and vertex areas are split in two, and
  the next sequence's low bit picks the slot. Before writing its slot, a
  draw waits until the batch that last used it is done, so at most one
  batch is outstanding. It then submits and returns without waiting.
- **Pieces.** Each piece of a long draw is one sequence with a slot of
  its own. A slot holds 298 triangles, or 238 with two texture units.
- **Kernel slots** carry signatures in the status page. A matching slot
  is used as it is, with no rebuild. A different one is rewritten only
  after the render engine drains.
- **The ring** is planned against a fresh HEAD with Gen3's
  `v9x_i9xx_ring_plan`.
- **The init state** is written into both slots' blocks, because draws
  read its viewports, scissor and default depth-stencil from their own
  state base.
- **The blitter wait.** A draw waits for the blitter only when the
  blitter's own pair says a blit is queued. The engine's `wait_idle`
  covers both rings since Phase 2, so calling it would bring the full
  wait back.
- **Synchronous cases.** The depth fill and the init batch stay
  synchronous. So does a draw whose target is offset 0 or the last
  accepted flip's buffer (plan decision 3).

The mini-VDD's `HSW_ENGINE_CLOSE` now waits, bounded, for render
dwords 8 and 9 to agree before it turns the rings off. It returns 2 if
they did not, and the 16-bit side then records `closed-render-busy`.

## Gates

| Gate | Result |
|---|---|
| 1. V9XDDP, fresh boot 39 | COMPLETE. 207 `Ok=1`; the same 17 `Ok=0` keys as every Phase 2 run (identical set). Differences: a callback address, a timing, and `ChainFrontRaw`/`SoloFrontRaw`, which read leftover video memory (now 0 after a fresh boot, 65535 in Phase 2's). 587 async and 12 sync submits; 8 batches refused, all mapping reasons the probe exercises (texture 3, texture op 4, colour key 7); none of reasons 11-16 |
| 2. V9XD32, all four of 16/32 bpp x Z16/Z24 | PASS, identical to boot 31 apart from texture handles |
| 3. Half-Life `-d3d` mwd5 | 51.044, 60.522, 60.436 fps (and 57.679 with screenshots running), all 393 frames. Mid-run frames whole. **All 1,072,860 draws synchronous**; see below |
| 4. 3DMark 2000 default, boot 40 | Score 2297. 15 frames captured across the run: every game and feature test drawn whole. Bump mapping skipped, as always |
| 5. Leaving exclusive mode mid-render | Partly. Alt-Tab and Escape injected by the agent do not reach 3DMark in exclusive mode. `WCLOSE.EXE 3DMark2000.exe` (`WM_CLOSE`) ended the demo mid-render: desktop restored, no timeout. Returning to a running benchmark was not tested |
| 6. Two Direct3D processes at once | **Not run.** Every probe here takes exclusive mode. Boot 39 ran V9XDDP, V9XD32 and Half-Life in turn on the same status page, so the shared sequence and kernel signatures crossed processes in sequence, not concurrently |
| 7. Disable and the engine close | **Not run.** The agent's `reset` is a hard reset (CF9h) that skips Windows' restart path, so it never calls Disable. The plan was wrong to name it. A live mode switch (`V9XMSW`, 800x600 to 1024x768x16) ran Enable again: the engine came back `open-reused`, and V9XD32 then passed all four combinations. The close did not run |

No gate saw an engine FIFO or idle timeout, a reset or a ring-space
wait.

## 3DMark 2000, against Phase 2

Same benchmark and settings. Each boot was clean before the run.

| | Phase 2, boot 38 | Phase 3, boot 40 |
|---|---|---|
| score (recorded, not chased) | 1991 | 2297 |
| batches | 1,532,479 | 1,712,897 |
| engine draw per batch | 157.4 µs | **126.9 µs** |
| waits for a slot that stalled | 0 of 1.53M | 161,881 of 1.72M (9.4%) |
| async / sync submits | 0 / all | 1,141,635 / 579,360 |
| kernel build per batch (`TimeDecode`) | 4.4 µs | 1.2 µs |
| vertex copy per batch | 60.3 µs | 60.3 µs |
| Flip per call | 1.22 µs | 2.22 µs |
| timeouts, resets, refused batches | 0, 0, 0 | 0, 0, 0 |

3DMark reported a 59 Hz refresh in its details, against 45 Hz on the
baseline run. Not looked into.

The plan predicted 85-95 µs a batch. This run measured 127 µs, for two
reasons the counters show:

1. **A third of the draws stayed synchronous.** The run uses triple
   buffering at offsets 0, `0x18C000` and `0x318000`. Every draw into
   the buffer at offset 0 matched the "GDI's primary" half of decision
   3: 579,360 of 1,720,995 submits, one in three.
2. **The GPU is the slower half** for some batches. 9.4% of slot waits
   found the other slot still running.

## Half-Life gets nothing, and why

Half-Life flips 23 times while starting (`FlipHandled=23`), then
presents by blitting from its render target at `0x96000`. The last
accepted flip was to `0x96000`, so `v9x_present_flip_offset()` stays
there. Every draw into that buffer then matches the displayed-buffer
rule (`DrawsIntoPresented` 1,071,292). Its fps is not evidence about
pipelining either way.

## What this leaves for decision

Decision 3's rule, "offset 0 or the last flip's buffer", is
conservative in both directions it was written for:

- the GDI screen at offset 0 is GDI's only while no application holds
  exclusive mode;
- the last flip's buffer is not on screen once the application presents
  by blit, or has returned to GDI.

Narrowing it needs knowing whether GDI owns the screen. `ddhal_core.c`
already handles the transitions, in `SetExclusiveMode` and
`FlipToGDISurface`. It has no state that `d3d_hsw.c` can read.

## Not covered

- Concurrent Direct3D processes (gate 6) and the engine close at
  Disable (gate 7).
- One capture during the demo showed a vertical seam: the left 315
  columns came from a different moment from the right. The agent's
  screenshot reads offset 0, which in a flip chain is a buffer being
  drawn, and the next 11 captures were whole. A tear on the monitor
  cannot be ruled out from captures. No one watched the monitor.
- Glide, 3DCIF and OpenGL on this build.
