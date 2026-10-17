# HD 4600 per-batch cost: the aperture is uncached, and the kernel was rebuilt in it; built in cached memory, mwd5 52.4 to 59.3 fps

Date: 2026-10-18. Machine: the i5-4570 (`8086:0412`), boots 21 and 22,
the HAL replaced by WININIT rename each time. Follows the Game 2 split
in `docs/issues/2026-10-17-haswell-3dmark99-flicker-and-game2.md`.
Evidence:
[`../probe/hsw-4570-batch-cost-2026-10-18/`](../probe/hsw-4570-batch-cost-2026-10-18/)
(the snapshots, and `buckets.ps1`, which turns two snapshots into the
tables below at 3.19 GHz).

## The aperture's memory type

`V9XBOOT.INI` (boot 20) records the MTRRs as the mini-VDD read them:

- `DEF_TYPE` `C00`: MTRRs on, default type UC;
- `Mtrr3`: base `E0000000` type 0 (UC), mask `E0000800`, an explicit UC
  range over `E0000000`-`FFFFFFFF`, which contains the 256 MB aperture;
- the read-only planner (`src/common/mtrr.c`) refused a write-combining
  plan (`r=11`).

So the reserve, which holds the state, kernels, vertices and batch, is
written uncached. A WC MTRR would not help, because UC wins where MTRR
ranges overlap. Making the range WC would take PAT, which is not tried
here.

## The split

`d3d_hsw.c` now also times, in Gen3 buckets V9XTRACE already names:

- the kernel build in `Decode`;
- the whole state write in `RingWait` (the kernel inside it is counted
  in both);
- the vertex copy in `RingWrite`.

Each run: Half-Life 1.1.1.0 `-d3d` at 640x480x16 full screen,
`timedemo mwd5` three times. The snapshots bracket one session.

| µs a batch | boot 21 (kernel built in place) | boot 22 (built cached, copied) |
|---|---|---|
| engine draw, whole | 75.5 | **65.3** |
| render-engine waits (two a batch) | 27.5 | 28.7 |
| kernel build | **16.1** | **3.2** |
| other state | 3.1 | 2.1 |
| vertex copy | 13.3 | 14.2 |
| batch build and submit (the rest) | 15.5 | 17.1 |
| mwd5, three runs | 46.782, 52.376, 52.207 | 52.010, 59.273, 59.087 |
| **best** | **52.376** | **59.273** |

- **The vertex copy writes about 280 dwords a batch** (11.6 triangles of
  three 32-byte vertices), at about 48 ns per uncached dword.
- **The kernel build cost about 210 ns per dword it produced.** The EU
  encoder ORs each field into its output (`hsw_eu.c`, `out[n] |= ...`).
  In place, each OR was an uncached read and an uncached write of the
  aperture.
- **The kernel is rebuilt for every batch.** It is a fixed function of
  its description, but the description includes the fog colour and the
  slot is shared between processes. So it is now built in a per-process
  cached buffer and only the words produced are copied into the slot:
  writes only, as many as before.

**+13% on mwd5 (52.4 to 59.3).** A boot-22 snapshot was not taken
without the timing buckets; they cost nothing measurable at boot 21
(52.4, as on boot 18 without them).

## What is left, by size

1. **Render-engine waits, 28.7 µs a batch.** Every batch is synchronous,
   and every batch carries the full pipeline flushes: CS stall, the state,
   VF, texture and instruction cache invalidates, and the depth stall,
   flush, stall. So about 28 µs is the GPU's latency for an
   11-triangle batch, with the CPU idle. Overlapping it would need more
   than one batch, state and vertex area in flight; lighter flushes would
   need measuring one at a time. Both risk a wedged RCS, so both are
   designs to write before building.
2. **Batch build and submit, about 17 µs:** about 190 dwords of state and
   commands per batch, all written uncached.
3. **Vertex copy, 14.2 µs:** the bytes themselves, uncached.

Write-combining (PAT) would shrink items 2 and 3 together. It is
CPU-wide state, so it is a design of its own.

Gates: check-tree, build-host, run-checks. The kernel change was not
tested on the host: the builder's output is the same words, built in a
different buffer.
