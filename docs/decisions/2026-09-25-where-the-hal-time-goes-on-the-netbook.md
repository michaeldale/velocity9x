# Where the HAL's time goes: 43% of 3DMark99 is the CPU waiting for the ring head

2026-09-25, MICHAEL-NETBOOK, boot 9, shared ABI 2026092502 (the timing
buckets). 3DMark 99 Max, all tests, 1024x576x16, triple buffering: **245**
3DMarks against 246 on the uninstrumented build, so the instrument costs
nothing measurable.

## The instrument

RDTSC around twelve HAL paths (`V9X_TIME_*` in `ddhal_internal.h`), Gen3
only because the 486s this HAL serves have no TSC. Cycles and call counts
accumulate in the shared block; V9XTRACE dumps them raw with a TSC/tick
calibration taken at each flip, and `scripts\report-hal-timing.ps1`
converts two snapshots into milliseconds. The TSC read 1,662,233 cycles a
millisecond over 398.7 s of flips, a 1.66 GHz Atom N280 as expected.

## The run (`...-before-V9XTRACE.ini` to `...-3dmark1024-V9XTRACE.ini`, 428 s)

| bucket | calls | ms | us/call | % of wall |
|---|---|---|---|---|
| D3D draw entry points | 59,867 | 213,462 | 3,566 | 49.9 |
| Gen3 draw, whole | 208,968 | 211,790 | 1,014 | 49.5 |
| decoder | 208,716 | 2,393 | 11.5 | 0.6 |
| ring write | 210,530 | 8,483 | 40.3 | 2.0 |
| **head wait** | 210,530 | **184,674** | **877** | **43.2** |
| breadcrumb wait | 208,716 | 152 | 0.7 | 0.0 |
| Flip | 459,986 | 1,577 | 3.4 | 0.4 |
| Lock | 64,921 | 80 | 1.2 | 0.0 |
| Blt copy | 0 | 0 | - | 0.0 |
| **Blt fill** | 3,116 | **31,471** | **10,100** | **7.4** |
| CreateSurface | 82,743 | 57 | 0.7 | 0.0 |
| application holding a Lock | 64,921 | 25,445 | 392 | 5.9 |

## What it says

- **The CPU spends 43% of the run spinning on RING_HEAD** after each
  submission, 877 us a batch of about 44 triangles. The core, the stream
  builders and the decoder are small (the engine's draw minus its waits is
  about 16 s, 4%); the D3D entry points add almost nothing over the engine.
- The breadcrumb costs nothing once the head is at the tail, confirming
  `BreadcrumbLagPollsTotal=0`: the head reaching the tail is effectively
  the batch being drawn.
- **Colour and depth fills are 7.4%**: 3,116 fills at 10.1 ms each, the CPU
  writing about 1.2 MB through the uncached aperture every time. Intel has
  no 2D engine ops, so every clear is a CPU fill.
- **The application's own writes through a Lock are 5.9%**, 392 us each,
  the same uncached path.
- What the HAL does not account for, about 36% of the wall, is 3DMark and
  the DirectX runtime themselves (transform, lighting, setup) plus idle.

## What it does not say

The head wait is the GPU consuming the batch, but it does not separate a
GPU that is slow at the work from a GPU paying a fixed cost per batch - the
leading MI_FLUSH_READ, the full state reload and the trailing MI_FLUSH that
every submission carries. At 877 us for 44 triangles the part is drawing
about 50,000 triangles a second, which is far below what a GMA 950 is
expected to do; nothing here says which of the two it is. Measuring the
head wait against batch size (3D WinBench's one-triangle batches beside
3DMark's 44) would separate them.

## What follows

In order of measured reach:

1. **Stop spinning on the head** (43%): return after writing the tail and
   wait only for ring space, and before Flip, Lock, Blt or a surface
   destroy touches memory the GPU may still use. This overlaps the
   application's ~36% with the GPU's work; it does not make the GPU faster.
2. **Find the per-batch GPU cost**: the head-wait-per-batch-size
   measurement, then drop the texture-cache invalidate and the trailing
   flush where nothing requires them, and merge same-state draws.
3. **Clears on the GPU** (7.4%): XY_COLOR_BLT, the one 2D operation this
   part has been measured doing correctly since Phase 4, for colour and
   depth fills.
4. **Write-combining on the aperture** for the application's Lock writes
   (5.9%) and any remaining CPU fills: a memory-type change needing its own
   measured decision, not attempted here.
