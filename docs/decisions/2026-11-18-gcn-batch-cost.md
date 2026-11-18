# GCN: the command window was uncached, and every 50 triangles flushed the GPU's caches

Date: 2026-11-18

Machine: W9QI0DAD (AMD FX-6300, Radeon Oland XT 1002:6610), 1024x768x32.
Builds: e6266162's working tree, `-dirty`. Evidence:
`build/driver-results/oland-batch-b100`, `oland-batch-b101`,
`oland-wc-b103` (not in the tree).

## Where it started

[The queued-flip record](2026-11-17-oland-queued-flips.md) left the
Oland's 3DMark 2000 at 8579 with vsync off and at the refresh with it.
Its counters said every batch was small and expensive:
- 133.9 M triangles went out in 2.65 M batches, about 50 each. The
  shared D3D core cut batches at 64 triangles (`V9X_D3D_INDEXED_BATCH`).
  The GCN back end takes 512.
- Each batch began with a shader-cache SURFACE_SYNC. It ended with an
  end-of-pipe event that flushed and invalidated the colour, depth and
  texture caches and wrote a fence. On GFX6 there was a second
  SURFACE_SYNC before that event.
- The largest driver cost on the CPU was `v9x_d3d_vi_draw`'s copy of
  vertices into the arena: 13.6 % of samples at boot 101, about 400 MB/s.

## What changed

1. **Batches up to 512 triangles on GCN.** A new limit,
   `batch_triangles`, is appended to `V9X_D3D_ENGINE_LIMITS`; the GCN
   tables name `V9X_D3D_VI_CHUNK_TRIANGLES`. The core's DrawPrimitives2
   run, list staging, two-unit staging and table-fog copy follow it, up
   to `V9X_D3D_BATCH_MAX` (512). Every other engine keeps 64.
2. **Fences per group, not per batch** (`vi_cp.c`). A D3D batch goes
   out with no fence of its own (`no_fence`). One standalone fence
   (`v9x_vi_3d_fence`) covers the group. It is written:
   - by any drain (Lock, SDMA work, waits for idle);
   - at the frame mark a queued flip waits for;
   - before a batch samples a surface drawn into since the last fence
     (`v9x_vi_cp_before_read`);
   - when an arena quarter has been left;
   - after 16 batches.
3. **The shader-cache sync only when it can matter.** That is after CP
   start, and when the arena enters another quarter. Only an earlier
   lap's data at an address can be stale. Shader code in the arena is
   followed by 256 unwritten bytes (`V9X_VI_CP_SHADER_PAD`), so the
   SQ's prefetch past a program's end cannot cache a line a later
   program is written into. The pad size is an assumption, not
   measured.
4. **The CP window write-combined.** The Oland row's MTRR covered the
   128 MiB mapping at the start of BAR0. The ring window at BAR0 +
   240 MiB lay outside it, in the BIOS default type, uncached. The
   ring, the vertex arena and the shader copies live there. Boot 101's
   V9XBOOT.INI shows it: BAR0 at D0000000, `MtrrWrite=ok s=4
   b=d0000001 m=f8000800`. A new row option, `V9X_HW16_WC_BAR256`,
   plans over the 256 MiB from BAR0's base. A window at 240 MiB makes
   BAR0 at least that large, and D0000000's alignment makes it exactly
   that. Boot 103: `m=f0000800`. The kicks already drained the
   write-combining buffers (`v9x_wc_drain`).
   - The same option goes to Carrizo, Mullins and every Evergreen row:
     the same window design, per the one-design-one-fix rule. None of
     them has run it.

## A bug the bigger runs found

Boot 100, with change 1, failed V9XD32 T13: the strip draw read almost
every triangle as culled. A run keeps one record end per record in
`record_ends[64]`. A 512-triangle run of one-triangle records wrote past
it, into the run's own fields. The array is now `V9X_R3D_RECORDS_MAX`
(512), and a capacity above it is refused.
`test_r3d_records.c`'s `test_long_run` crashed the host tests before the
fix and passes after. The two DX5 paths that keep a run on the stack
went past the HAL's 2048-byte frame limit with the larger array. Their
runs are static now: the Win16 mutex serializes those callbacks.

## Measured

V9XD32 after each deploy: T1-T16 and T22 pass. T4 and T14 have 0
mismatches. The only failure is T19 FfSpecFog, the known issue. No run
had an engine timeout, a forced-idle flip or a draw into the presented
buffer.

| boot | build | benchmark | vsync | score | before |
|---|---|---|---|---:|---:|
| 101 | 1-3 | 3DMark 2000, V9XPROF | game's | 4104 | 3985 |
| 102 | 1-3 | 3DMark 2000, V9XPROF | off (VSync=2) | 9802 | 8579 |
| 104 | 1-4 | 3DMark 2001 SE, grabs | game's | **3670** | 2601, 2603, 2616 |
| 108 | 1-4 + indexed (below) | 3DMark 2001 SE, grabs | game's | **3989** | 3670 |

- 2001 SE's "before" runs are [the three clean runs after the
  read-pointer fix](../issues/2026-11-17-oland-3dmark2001se-cp-stall-hang.md):
  same machine, same drive script, grabs on in both.
- The score dialog says the benchmark "was not run using default
  settings". It is the same install as the earlier runs, but whether the
  settings were identical was not checked.
- Batches in 3DMark 2000: 2.65 M to 1.02 M with vsync, 3.9 M to 2.1 M
  without. In 2001 SE, 825,216.
- The grabbed 2001 SE frames are whole (`grabs-sheet.png` in the
  evidence). Car Chase Low runs at 30-60 fps there, so part of 2001 SE
  is held at the refresh too.

## Second pass: where 2001 SE's CPU time went, and indexed draws

A V9XPROF run of 3DMark 2001 SE on boot 104 (score 3930, no grabs)
put the D3D runtime's software T&L at 5.7 % of samples, and the driver
at 67.7 %. Split into 10-second windows, the driver's costs were:

| cost | run | in the tests |
|---|---:|---:|
| waiting for vsync in Flip (`eg_scanout`; KERNEL32's 14.9 % beside it) | 21.8 % | 10-60 % |
| the CPU waiting for the CP to go idle (`v9x_vi_cp_poll_fence`) | 10.0 % | up to 63 % |
| the driver's own per-vertex work (DP2 walk, conversion, record copies, culling, staging) | 19.7 % | 25-56 % |
| `v9x_d3d_vi_bc_copy`, a DXT level read back byte by byte on Lock | 8.1 % | about 0: loading screens |

The run sent 463 M triangles in 5.7 M DrawPrimitives2 records, most as
indexed lists, which the core expanded a triangle at a time: every
corner converted, copied and culled again for each triangle sharing it.
And each of the 45,208 clears went to the DMA engine after the CPU
waited for the CP to drain.

**Tried and dropped: waits on the GPU.** The DMA engine polled the CP's
fence (SI DMA POLL_REG_MEM, as amdgpu's si_dma pipeline sync), and the
CP waited on the DMA fence (WAIT_REG_MEM, as gfx_v6_0's), in place of
the CPU's drains.
- With Linux's equal compare the CP hung at boot 105: given up waiting
  for ring space, stopped at a WAIT_REG_MEM for DMA fence 183h. The
  score that run showed, 5364, was of a run whose CP was given up early,
  and is not a result.
- With greater-or-equal on both sides (boot 107) nothing hung, but 3DMark
  2001 SE fell to **1464** from 3670. Why the GPU-side waits cost that
  much was not established (the DMA poll's interval and retry semantics
  are not documented here).
- Reverted: the CPU drains again. The CP's next batch now syncs its
  caches after any DMA operation (`v9x_vi_cp_note_dma`).

**Indexed lists drawn whole on the Oland** (`v9x_d3d_vi_draw_indexed`).
The TL draw takes an index buffer (`DRAW_INDEX_2` of 16-bit indices,
`INDEX_TYPE` before it, radeonsi's packets; host-tested). Every GCN VS
reads its vertex by VertexID, which an indexed draw sets to the index.
The core hands an engine that opts in (`indexed_pool_vertices`,
`indexed_two_units`) the call's vertices converted once each: only the
ones its indices name, compacted, the indices rewritten to them
(`v9x_d3d_dp2_compact`). Two-stage calls go too, their pairs per pool
vertex. The engine culls on the CPU by the core's rule, unrolls strips
with the core's winding swap, and copies the pool and the surviving
indices to the arena once. Other engines keep the 192-vertex gather and
one stage.
- Boot 106, the first version (pools copied from the lowest index to the
  highest), with the GPU waits: 1775.
- Boot 108, compacted, CPU drains: **3989**, against 3670 (+8.7 %).
  2,110,750 calls drawn whole, none declined; no engine timeout; V9XD32
  T13's indexed draw now takes this path and passes; the grabbed frames
  are whole.
- `DrawsIntoPresented` read 13 in both indexed runs, 0 on the list path
  before. Unexplained; the grabs show no damage.

## Not established

- One run of each. The 2001 SE gain is 1,054 points over three runs that
  agreed within 15.
- Changes 1-3 and change 4 were not measured apart on 2001 SE. 3DMark
  2000 shows 1-3 alone: +14 % with vsync off.
- The vertex copy's new cost per byte was not profiled after change 4.
- Carrizo, Mullins and the Evergreen cards have run none of this.
- The SQ's prefetch distance past a program's end is assumed to be
  within 256 bytes.
