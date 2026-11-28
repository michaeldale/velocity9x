# GeForce 210 / 9500 GT: let the CPU write the next 3D batch while the GPU draws this one

Status: steps 1-2 built and measured 2026-11-28 on branch
`nvidia-push-ring`; not shipped: profiled 3DMark 2001 SE scored lower
with the ring and single tests moved by factors of two
([record](../decisions/2026-11-28-nvidia-push-ring-not-shipped.md)).
Evidence for the plan:
[2026-11-28-g96-pushbuffer-write-combined.md](../decisions/2026-11-28-g96-pushbuffer-write-combined.md).

## Why

With the pushbuffer write-combined, a profiled 3DMark 2001 SE run on the
9500 GT spends 32% of its samples in `v9x_nv50_read32`, polling.
`v9x_nv50_3d_begin` drains the engine before every batch because there
is one 3D pushbuffer (768 KiB at reserve + 1 MiB) and one set of
per-batch tables, so the CPU and GPU take turns. Overlapping them saves
at most the smaller of the two halves; from the profile, the CPU's half
(the HAL's work outside the wait, D3D8, the program) is of the same
order as the wait, so the ceiling is large, but it is a ceiling, not a
measurement.

## What a batch shares today (`d3d_nv50.c` v9x_d3d_nv50_draw)

- the push area, inline vertices included: read by the pusher when it
  fetches the batch;
- TIC and TSC entries in the scratch (`V9X_NV50_3D_SCRATCH_TIC/TSC`),
  written per batch: read by PGRAPH while it draws;
- the vertex program and fragment variants (`write_programs`, at setup)
  and the FP2 and pixel shader slots (written when a new combination
  appears; the PS slot comment relies on every batch draining);
- the IB ring index: `v9x_nv50_3d_submit` takes PFIFO's IB GET as the
  next entry, which is right only when everything before was fetched.
  The 16-bit GDI arm (`gdi_accel.c`) kicks the same ring under the same
  rule.

## Design

1. **A fence.** A semaphore release at the end of each batch writes its
   sequence number to a word in the reserve (NV50's channel semaphore
   methods, or the 3D class's QUERY_GET; nouveau's fence for this
   generation, `nouveau_fence`/`nv50_fence`, is the reference). The CPU
   reads that word to know which batches finished executing, without
   touching registers.
2. **Slots.** The push area split into N slots (4 x 192 KiB), each with
   its own TIC/TSC block in the scratch. A batch takes the next slot,
   waits only until that slot's previous batch has signalled, points
   TIC/TSC at its block (TIC_ADDRESS/TSC_ADDRESS plus their flushes),
   and submits at IB PUT, not GET, checking the ring is not full.
3. **Programs.** Writing a new FP2 or PS slot, or the setup programs,
   waits for the fence of every batch in flight (a full drain, as now):
   it is rare.
4. **GDI and DirectDraw.** Every path that drains today keeps draining
   (Lock, Blt, Flip, the shadow copy-back); the 16-bit side's rule
   (wait for everything fetched, then take GET) stays valid because it
   waits first.
5. **Host tests** for the slot and fence arithmetic (wrap, ring full,
   which slot may be written), in the `nv50_chan.c` pattern.

## Steps

1. The fence alone, still draining per batch: measure that the word
   advances once per batch and that a deliberate wait on it matches the
   drain (V9XD32, 3DMark 2001 SE, Quake 3).
2. Two slots with per-slot TIC/TSC; then four if two measure better.
3. The profile again, and V9XD32, V9XGDI, 3DMark 2001 SE, Quake 3 demo,
   recorded.

Applies to the GeForce 210 as well (same engine and reserve).
