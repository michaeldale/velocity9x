# GCN (SI/VI): the CPU waits for the GPU to go idle at every flip, lock and DMA fill

Date: 2026-11-18

Seen on: M-XP7053, Radeon R7E (Carrizo, GCN 3), 0.54.0, 3DMark 2001 SE
under V9XPROF ([record](../decisions/2026-11-18-carrizo-3dmark2001se-profile.md)).
The code is shared by the Oland and Mullins. Status: open, design
limit, not a crash.

## What was measured

`vi_cp!v9x_vi_cp_poll_fence` took 7.1 % of all samples in a whole
benchmark run. It is a pure spin: it reads the fence dword in the
window until the CP's last sequence lands. The CPU does nothing else
meanwhile.

## Where the full drains come from (from the code at f93b15e3)

`v9x_vi_cp_drain(1)` waits for every queued batch. It is reached from:
- `eng_vi.c` `v9x_vi_wait_idle`, the engine's `wait_idle`, which
  `ddhal_core.c` calls:
  - before a Flip;
  - before a Lock;
  - before a Blt that must not race the engine;
- `eng_vi.c` `v9x_vi_after_cp`, before every SDMA fill or copy, so the
  DMA engine never touches a surface the CP is still using;
- `vi_cp.c` `v9x_vi_cp_vertex_space`, when the 2 MiB vertex arena wraps.

So a frame that ends in a flip, and any colour fill on the DMA engine,
waits for all of the GPU's work so far. The CPU then builds the next
frame while the GPU sits idle. The two never overlap. How often each
caller drained in this run was not counted.

## Ideas, none tried

- Wait at a flip only for the batches that draw into the surface being
  shown, using the fence of the last batch that targeted it, not the
  newest fence.
- Let the SDMA engine wait for the CP's fence on the GPU (a semaphore or
  a wait-register-memory packet), not by the CPU spinning.
- Before a Lock, wait only when a queued batch touches the locked
  surface.
- Grow the vertex arena, or reuse it in halves fenced separately, so a
  wrap waits for the older half only.

Each of these needs its own measurement. The first gate would be the
same V9XPROF run, with `poll_fence`'s share and 3DMark's frame time
recorded before and after.
