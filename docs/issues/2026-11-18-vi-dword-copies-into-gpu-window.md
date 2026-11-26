# GCN (SI/VI): ring and vertex copies are dword loops into the GPU window

Date: 2026-11-18

Seen on: M-XP7053, Radeon R7E (Carrizo, GCN 3), 0.54.0, 3DMark 2001 SE
under V9XPROF ([record](../decisions/2026-11-18-carrizo-3dmark2001se-profile.md)).
The code is shared by the Oland and Mullins. Status: open.

## What was measured

Two loops, each about 7 % of all samples in a whole benchmark run:
- **`vi_cp.c` `v9x_vi_cp_put`, at +155h**, 6.9 %: the command stream is
  copied into the ring one dword at a time. Each iteration also stores
  `v9x_vi_cp_wptr`, a global, back to memory (the disassembly at
  07AAh-07C5h).
- **`d3d_vi.c` `v9x_d3d_vi_draw`, at F1Dh-F39h**, 7.3 %: each batch's
  vertices are copied from the stack into the vertex arena one dword at
  a time, recomputing both addresses from the stack every iteration.

Both destinations are in the 4 MiB window at BAR0 + 240 MiB. The driver
writes it through write-combining, given `v9x_wc_drain()` before each
kick. On this APU the window reaches the carve-out of system memory
through the GPU. How fast that path is per byte was not measured.

## Ideas, none tried

- Copy in one `rep movsd` (or a memcpy the compiler emits as one), with
  the write pointer updated once after the copy, not per dword.
- Build vertices straight into the arena instead of on the stack and
  then copying (`d3d_vi_draw`), when the batch is known to fit.
- Put the ring and vertex arena in system memory mapped for the GPU
  (GART), so the CPU writes ordinary cached memory. This is larger, and
  needs the GPU's snooping or a flush measured first.

The first two are local to `vi_cp.c` and `d3d_vi.c`. Their gate: the same
V9XPROF run, with these two loops' shares before and after, plus V9XD32
and a 3DMark 2001 SE run without a stall.

## 1.0 re-read, 2026-11-26

Moved after 1.0: listed in the 1.0 plan's "Not in 1.0". Performance
work, listed in the 1.0 plan.
