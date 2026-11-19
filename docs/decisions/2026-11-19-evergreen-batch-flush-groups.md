# Evergreen: batches as long as a call, caches flushed per group; Trinity 3DMark 2000 3821 to 4130, 2001 SE 1906 to 2462

Date: 2026-11-19. Machine: M-D7TFW4 (W9QI6DFC), A10-5700, HD 7660D
(Trinity, `1002:9901`), remote help, 1024x768x32 desktop. Baseline: the
[released 0.56.0](2026-11-19-trinity-0.56.0-baseline.md) and the
[XRGB8888 fix](2026-11-19-evergreen-xrgb8888-textures.md) (`1993c5eb`).
Evidence: [`../probe/trinity-d7tfw4-batch-2026-11-19/`](../probe/trinity-d7tfw4-batch-2026-11-19/)
(step 1) and
[`../probe/trinity-d7tfw4-groupflush-2026-11-19/`](../probe/trinity-d7tfw4-groupflush-2026-11-19/)
(step 2), each with the runner's record, score screens and V9XTRACE
snapshots.

## Why

The GCN change `390f9dfa` ([record](2026-11-18-gcn-batch-cost.md)) gave
the Oland batches up to 512 triangles and one cache flush per group of
batches instead of per batch. The Evergreen path had neither: its engine
limits stopped before `batch_triangles`, so the core cut runs at 64, and
every batch ended with `v9x_eg_3d_flush`, a SURFACE_SYNC flushing CB and DB
and invalidating TC over all of memory, which waits for the pipe.
Trinity's 3DMark 2000 run on 0.56.0 sent 174.5 M triangles in 3.64 M
batches.

## Step 1: `batch_triangles` 512 (boot 76)

`d3d_eg.c`'s limits name `V9X_D3D_EG_CHUNK_TRIANGLES` (512), what one
`v9x_d3d_eg_draw` call already took.

- 3DMark 2000: 3823 (3821). Batches 3.64 M as before: its calls average
  48 triangles (`R3dListCalls` 3,645,686), so each was one batch already.
- 3DMark 2001 SE: **2370** (1906). Its calls are larger.
- Every release probe passes (V9XD32 at 32 and 16 bpp, GL, Glide, R3DP,
  WPRS, FBCMP).

## Step 2: one flush per group (boot 77)

`eg_cp.c`, GCN's design without fences (the Evergreen CP drains by idle
poll):
- a D3D batch goes out without the flush (`v9x_eg_cp_submit_open`), and
  the colour target and depth buffer it drew into are listed (up to four);
- `v9x_eg_cp_close` puts the flush on its own: after 16 batches, when a
  fifth surface would not fit the list, before a batch samples a listed
  surface (`v9x_eg_cp_before_read`), and at every drain. Every reader from
  outside the pipe drains first: Lock and wait-idle, flips
  (`eg_scanout.c`), the DMA engine (`eng_eg.c`) and the vertex arena's
  wrap.

Results:

| | 0.56.0 (boot 73) | step 1 (boot 76) | step 2 (boot 77) |
|---|---:|---:|---:|
| 3DMark 2000, default, 1024x768x16 | 3821 | 3823 | **4130** |
| 3DMark 2001 SE, saved Low detail | 1906 | 2370 | **2462** |

One run each, same installs and settings, no hang, no engine timeout
(`EngineIdleTimeouts=0`). V9XD32 passes at 32 and 16 bpp with failures 0;
every other probe passes.

## Not covered

- The frames: no grabs were taken and nobody looked at the screen. A
  missed flush would show as stale texels in a surface drawn and then
  sampled, or a half-drawn frame. V9XD32's checks read back through Lock,
  which drains.
- Cedar and Caicos (Evergreen proper), which take the same code: no
  machine online.
- No host test: `eg_cp.c` and `d3d_eg.c` are HAL code the host tests do
  not build; the group bookkeeping is GCN's, which has run since
  2026-11-18.
- The default state (262 dwords) still goes with every batch.
