# HD 4600: make the Direct3D T&L device faster than the runtime, then offer it

Date: 2026-11-21. Status: planned; Phase 0 next. Nothing here has been
built or measured. Continues step 6 of [`hardware-tnl.md`](hardware-tnl.md)
("measure where a batch's time goes before another design"). Claims about
the tree cite files at `63d140af`.

Machine: the i5-4570, HD 4600 (`8086:0412`), `haswell-4570-test-machine`.

## Why now

The device was built and switched off on 2026-10-27
([record](../decisions/2026-10-27-haswell-tnl-step6-d3d-device.md)):

| | T&L device (boot 99) | runtime T&L (boots 92, 97) |
|---|---|---|
| 3DMark 2000 Pro, default | 1558 | 2322 |
| 3DMark 2001 SE, saved settings | 722 | 1120 |

All of those numbers predate four changes to the same engine, each
measured only on the CPU T&L path since:

| Change | Record | CPU-path effect |
|---|---|---|
| PAT write-combining, 2026-10-28 | `2026-10-28-haswell-pat-write-combining.md` | 3DMark 2000 2344 to 3719 |
| One RT flush dropped, 2026-10-28 | `2026-10-28-haswell-flush-variants.md` | 3DMark 2000 3731 |
| RP0 at engine open, 2026-11-14 | `2026-11-14-haswell-render-clock-400mhz.md` | the GT ran at 400 MHz, not 1150; 2001 SE 2357 to 3284 |
| Batch a slot long, indexed pieces, 2026-11-18 | `2026-11-18-haswell-batch-size-and-indexed-pieces.md` | 2001 SE 3388 to 3888 |

The T&L device was measured at a third of the GPU's clock and without
write-combined vertex writes. Whether it is still slower is not known.

## What the tree has (63d140af)

- **The switch.** `V9X_D3D_TNL_DEVICE_OFFERED` 0 (`d3d_core.c:254`) hides
  `D3DDEVCAPS_HWTRANSFORMANDLIGHT`, the lights and the vertex processing
  caps (`d3d_core.c:4792`, `:8420`). Everything behind it is compiled and
  host-tested.
- **The engine.** `tnl_available` is true on Haswell only; Ivy Bridge
  (`ENGINE_CAP_GEN7_IVB`) is excluded (`d3d_hsw.c:2277`). Two-sided keys
  are declined (`d3d_hsw.c:2287`).
- **The batch.** Up to 400 vertices and 1,200 indices, with a 64-entry
  vertex cache keyed by the pool address (`d3d_core.c:2469-2471`). The
  render ABI allows 480 and 1,536 (`r3d_abi.h:386`). One `draw_tnl`, so
  one whole pipeline, per batch.
- **The vertices.** Read from user memory or from the vertex surface
  (`d3d_core.c:8053`), converted and copied each draw. No driver vertex
  buffers: nothing implements DX7's execute-buffer callbacks.
- **Which programs reach it.** Only DDI 7 processes: DirectX 7 programs.
  DirectX 8 and 9 programs get DDI 8, whose vertex processing is the
  runtime's (`ddi8-on-win9x.md` section 3; `d3d_core.c:4656-4668`). So
  **3DMark 2001 SE no longer reaches the device at all**; step 6 measured
  it at DDI 7, before DDI 8 existed. The gate here is 3DMark 2000 and DX7
  titles. Hardware vertex processing for DX8 programs is a separate plan
  (see "Not in scope").

## What to expect

From the 2026-11-21 research (sources in the session record, not
re-checked here):

- **Fast CPUs often matched or beat early hardware T&L.** Beyond3D's
  3DMark 2000 analysis had a PIII and an Athlon beating the GeForce 256 in
  the high-polygon tests. Intel's own G965-class drivers routed a list of
  games, and reportedly 3DMark, to software vertex processing for a
  reported 30-65 % gain.
- **The per-draw cost decides it.** NVIDIA's GDC 2003 figure: about 50k
  draw calls a second saturate a 1.5 GHz Pentium 4. Step 6's 3DMark 2000
  context drew 855,305 batches of about 98 triangles. At the measured
  ~10 µs of GPU time per batch (`2026-10-28-haswell-flush-variants.md`),
  that is about 8.5 s of fixed cost per run.
- **The EUs are not the limit (estimate, not measured).** 20 EUs at
  1150 MHz, two vertices a SIMD4x2 thread, about 100 instructions for
  eight lights: hundreds of millions of lit vertices a second. 3DMark
  2000 drew 83.6 M triangles in a whole run.
- **Expectation for this machine:** parity, or a modest win in
  high-polygon lit scenes, once batches are large and vertex-buffer
  vertices are not copied. The runtime's SSE T&L on an i5-4570 is cheap.
  The case for the device rests as much on titles that require the cap,
  and on being the base for DDI 8 hardware vertex processing, as on speed.

## Phase 0: measure the device on today's tree (one boot)

A measurement build: today's `main` with `V9X_D3D_TNL_DEVICE_OFFERED` 1,
named as dirty in the record. Not committed with the switch on.

**Counters** (added to the HAL's diagnostics and committed, so later
phases measure the same way):

- T&L batches; triangles and vertices a batch;
- vertices referenced against vertices converted (the cache's hit rate);
- why each batch closed: full, a T&L record, a render state, a
  pre-transformed run, the end of the DP2 call;
- keys built, kernel-cache misses, constant bytes packed;
- DP2 calls whose vertices come from a vertex surface against user
  memory. This decides whether Phase 3 is worth doing.

**Runs, all on one boot.** The device is offered and 3DMark 2000 lets the
user choose software or hardware T&L, so A and B need no reboot:

1. V9XD32: `Result=PASS` with T18.
2. 3DMark 2000 default, hardware T&L, then software T&L. Baseline: 4011
   (boot 272, CPU path).
3. 3DMark 2000's high-polygon feature tests at 1 light and 8 lights, in
   both modes. They give triangles a second, which splits the per-vertex
   cost from the per-batch cost.
4. The hardware-T&L default run again under V9XPROF, for where the HAL's
   CPU time goes.

The exact names of 3DMark 2000's T&L choices are read on the machine
first, not taken from memory.

**What each result rules in or out** (step 6's three hypotheses):

| Hypothesis | Ruled in if | Ruled out if |
|---|---|---|
| Per-batch cost | 1-light test behind CPU, 8-light test ahead; profile dominated by batch build and submit | batches already average several hundred triangles |
| Vertex cache too small | converted/referenced well above 1 | about 1 |
| EU lighting cost | 8-light test behind CPU while 1-light is ahead | 8-light ahead |

The record goes in `docs/decisions/` with what each result rules out. If
the device already beats the CPU path on 3DMark 2000's default run,
Phases 1-2 are skipped and Phase 4 is next.

## Phase 1: cheap fixes, chosen by Phase 0

Only the ones Phase 0 points at:

- **Batch room:** raise 400/1,200 towards the ABI's 480/1,536 and beyond
  if the vertex area allows. The vertex area grew in the 2026-11-18
  batch-size work.
- **Cache:** larger, or keyed so that a pool's vertices convert once per
  DP2 call.
- **Constants and key:** keep the last key and block per context, and
  repack only what a record changed.
- **Closes:** a render state that changes no T&L input should not close
  a batch. Today any T&L render state closes it.

Gate: 3DMark 2000 default and the two feature tests, same runs as
Phase 0, V9XD32 PASS.

## Phase 2: T&L draws in multi-draw batches

The fixed per-batch cost is the subject of
[`intel-haswell-multi-draw-batches.md`](intel-haswell-multi-draw-batches.md),
which already names the T&L draw as a draw in the open batch (its
Phase 3). That plan goes first; this phase adds what T&L needs on top:

- per draw: the VS kernel pointer and the push constants (CONSTANT_VS),
  as a delta when the key or block changed;
- the VS, CLIP and SF viewport state set once a batch when unchanged;
- step 6's segmented batch, which lost (1292 against 1558), retried only
  if Phase 0 shows that state segments, not batches, are what remains.

Gate: as Phase 1, plus the counters showing draws per batch.

## Phase 3: DX7 driver vertex buffers in GTT memory

Only if Phase 0 shows a useful share of vertices coming from vertex
surfaces.

1. **Read first.** The DX7 DDK on driver-allocated vertex buffers: the
   execute-buffer callbacks (`DDHAL_DDEXEBUFCALLBACKS`: CanCreate,
   Create, Destroy, Lock, Unlock), `DDSCAPS_EXECUTEBUFFER` in video
   memory, and how the runtime decides to ask the driver. That these are
   the callbacks involved is recalled, not read. The output is a spec
   note naming each field.
2. **Allocate** vertex buffers from the GTT heap, write-combined, and
   lock them without a copy. A lock waits only for the draws that read
   that buffer.
3. **Fetch in place.** The VF reads the FVF layout directly: position,
   normal, D3DCOLOR as B8G8R8A8_UNORM, texture pairs. A layout the VF
   cannot express keeps the convert-and-copy path.

Gate: the counters show no CPU copy for vertex-buffer draws; 3DMark 2000
and V9XD32 as before.

## Phase 4: nothing dropped

Before the device is offered by default:

- **A CPU fallback.** Untransformed points and lines, and batches the
  engine declines, are dropped today. Linking a CPU transform into the
  HAL, from the reference evaluator or a lean version of it, is a
  design change from `hardware-tnl.md`, which keeps the evaluator out of
  the HAL. It needs Michael's agreement first.
- **Ignored state, counted.** Texture generation, TCI flags, texture
  transforms and vertex blending are not drawn; caps say so. A draw that
  sets them anyway is counted, and Phase 5's title runs show whether any
  title does.

Gate: a V9XD32 case with untransformed points and lines on the T&L
device, compared with the HAL device.

## Phase 5: on by default

Decision 1 of `hardware-tnl.md` returns: the switch goes away, not into
an INI key (`no-gated-defaults-dev-driver`). Gates, on one boot:

- 3DMark 2000 default on hardware T&L at or above software T&L;
- V9XD32 PASS with T18;
- one DX7 title that uses the T&L device, played. Which titles are on the
  4570 is checked first; none is named here from memory;
- the release probes through `run-release-checks.ps1 -Machine hd4600`.

The per-program DDI override is the escape for a DX7 title that breaks
on the device.

## Not in scope

- **DDI 8 hardware vertex processing.** DirectX 8 programs, among them
  3DMark 2001 SE and the titles that reportedly refuse to start without
  hardware T&L, see `HWTRANSFORMANDLIGHT` only through D3DCAPS8. That
  needs DDI 8's stream and vertex-buffer handles feeding the same kernels,
  and it is the natural next plan once this one passes.
- Vertex shaders 1.1.
- OpenGL: two-sided lighting (SBE facing), and making
  `EXT_compiled_vertex_array`'s lock keep vertices between passes; today
  the lock is state only (`3faeae06`).
- Ivy Bridge and the other Intel generations.

## Risks

- A measurement build offers the device to every DX7 program on that
  boot, DDHELP included. The cross-process fault of boots 101-102 is
  fixed, but this is the first build with the device on since boot 100.
- Phase 3 locks buffers the GPU may be reading. A wrong wait there is a
  corrupted frame or an engine hang, and a hang means Michael power-cycles
  the 4570.
