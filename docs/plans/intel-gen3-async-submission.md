# Intel Gen3: submit without waiting for the ring head

Date: 2026-09-25. Status: plan, nothing coded. Needs an errata decision
(below) before the first build runs on the netbook.

## Why

Every Direct3D batch on the 945GSE is written to the ring, and then the
CPU polls `RING_HEAD` until the parser reaches the tail and polls the
breadcrumb until the pixels have landed (`v9x_d3d_i9xx_ring_submit`,
`d3d_i9xx.c`). The CPU and the GPU take turns.

Half-Life at 640x480x16 on boot 31
(`docs\decisions\2026-09-25-netbook-halflife-alpha-test-V9XTRACE.ini`),
79 s since the first Direct3D call, menus and loading included:

| | seconds | share |
|---|---|---|
| All Direct3D calls (`TimeD3dCalls`) | 51.8 | 65% |
| Waiting on the ring head (`TimeHeadWait`, 933,256 batches) | 33.6 | 43% |
| Building and writing batches (`TimeEngineDraw` less the head wait) | ~14.5 | ~18% |
| Waiting on the breadcrumb after the head (`TimeCrumbWait`) | 0.7 | 1% |
| Blits, now on the engine | 1.9 | 2% |

The head wait is the GPU drawing: `2026-09-25-the-gen3-head-wait-is-pixels.md`
measured its cost by batch shape and found area, not batch count, drives it.
Overlapping it with the CPU's work cannot make the GPU draw faster; it can
stop the CPU idling while it does.

**The ceiling is workload-dependent, and stated as such.** If the CPU's
remaining work and the GPU's overlap perfectly, a frame costs the larger
of the two instead of their sum. For this Half-Life run that is at best
roughly a third less time. For 3DMark 99 at 640x480 the head wait was 2.7%
of the run and there is almost nothing to gain; at 1024x576 it was 43%
before the stride fixes and has not been re-measured since.

## What changes

1. **`v9x_d3d_i9xx_ring_submit` stops waiting.** It writes the stream,
   writes `TAIL`, records the batch's breadcrumb sequence as the one
   outstanding, and returns. The head is read only to plan free space.
2. **The ring-full wait becomes the only wait inside a submit.** When
   `v9x_i9xx_ring_plan` reports too little room, poll `RING_HEAD` - bounded,
   counted - until it does, then plan again. The ring is 64 KiB; a
   Half-Life batch is a few hundred bytes, so dozens are in flight before
   this triggers.
3. **Completion stays the breadcrumb, and stays one sequence.** One engine
   runs its commands in order, so the latest sequence arriving means every
   earlier batch is done. `v9x_d3d_i9xx_render_drain` already waits for
   exactly that, bounded, with the abandon path behind it.
4. **Every place the CPU or the display touches GPU memory drains first.**
   Already: Flip, Lock and the CPU blit fallback (`ddhal_core.c`), and the
   engine blits, which are ordered on the same ring. To be added and each
   justified in code:
   - DestroySurface of anything a queued batch may still read or write -
     a texture, a Z buffer, a render target - and the HAL's own frees of
     placed mip trees and textures (`v9x_d3d_i9xx_destroy_surface`),
     because DirectDraw's heap can hand the block to a new surface at once;
   - D3D context destroy and DriverInit / mode change;
   - any HAL path that writes video memory with the CPU without a Lock
     (the texture and Z placement code is to be audited for this, not
     assumed clean).
5. **The head-wait instruments move.** `TimeHeadWait` would read near
   zero and stop meaning anything. Replace it with the ring-full wait and
   the drain time at each site, so the capture still says where the CPU
   waits.

## Hazards, named before the first build

- **Stale texels, silently.** A missed drain does not fault: a batch reads
  a texture the application has already rewritten, or a freed block
  reused, and the picture is wrong. The drain sites are listed in the
  code and each one says what it protects.
- **GDI writes the primary with the CPU and cannot see the ring.** In
  full-screen exclusive mode Direct3D draws to back buffers and the
  present is a ring blit, so GDI has nothing to race. A windowed Direct3D
  application renders into memory GDI also writes. This plan does not fix
  that; it records whether it is seen, and a windowed test belongs in
  the verification.
- **Failure detection moves.** Today a wedged ring shows as a head timeout
  on the next submit. After this it shows at the next ring-full wait or
  drain. Both are bounded; the abandon path stops the channel either way.
  No reset is added (as for the blits, plan decision 9).

## Errata, assessed rather than inherited

Erratum 12 (Intel 309220-0132, no trigger published, no fix on A3) is about
a sequence of CPU and GPU memory accesses. Today the CPU never touches
memory while the GPU is drawing: every submit waits. After this, the CPU
writes vertices into the ring, texels through Locks and Direct3D's own
buffers while the GPU is executing earlier batches. That is a new
interleave, not a denser version of an authorised one, so it needs its own
one-line decision before the first boot - the same shape as
`2026-09-25-intel-2d-blits-errata-gate.md`.

## Verification

1. **Host.** `v9x_i9xx_ring_plan` already covers wrap and free space; add
   the ring-full retry arithmetic as a pure function with tests.
2. **The same workloads, before and after, one boot each.** 3DMark 99 at
   640x480 and 1024x576 (692 and 643 before), and Half-Life with
   `timedemo` on a shipped demo for a frame rate that does not depend on
   where the player walked.
3. **Counters.** Ring-full waits, drains per site, `BreadcrumbTimeouts`,
   `BreadcrumbAbandoned`, `EngineResets`; texture and surface counts
   unchanged against the synchronous run.
4. **The picture.** The operator's eye on Half-Life and one 3DMark run -
   stale texels are the failure this cannot count.

## Not in scope

- Batch merging (joining consecutive draws with the same state). Half-Life
  averages 3.7 triangles a batch (3,480,923 in 933,256), so it is the
  next lever once the CPU is the limit, and measured then.
- Anything that makes the GPU fill faster, including the 1024x576
  per-pixel cost still open in the head-wait decision.
