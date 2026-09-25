# Intel Gen3: DirectDraw blits through the ring

Date: 2026-09-22. Status: builds 000-002 landed together on 2026-09-25
(fill, depth fill and non-overlapping copy);
`docs\decisions\2026-09-25-gen3-engine-blits-half-life.md` has the
measurement and `2026-09-25-intel-2d-blits-errata-gate.md` the errata
decision. Build 003 (overlap) is open.

The Intel family runs DirectDraw today with `V9XHAL.DLL (vidmem + flip, CPU
blits only)`: `v9x_engine32()` knows the two S3 tables and nothing else, so
every `Blt` on the 945GSE is completed by `src\display32\blt_cpu.c` through
the aperture. The ring that would do it in hardware is already up, already
submitting 3D batches and already carrying the page flip. This plan closes
that gap and only that gap.

**Not in scope: GDI acceleration.** The 16-bit driver has no runtime reach
into the ring and two producers on one tail pointer is an unanswered
arbitration question. That is a separate plan; nothing here creates it or
depends on it.

## Why this is cheap

Every part that was expensive on this chip is built and measured:

| Piece | Where | Evidence |
|---|---|---|
| Ring up, head follows tail, wrap works | mini-VDD `RingOpen`, [gma950_hw16.c:156](../../src/chipsets/intel/gma950/gma950_hw16.c) | [Phase 4](../decisions/2026-09-14-intel-phase4-first-write-the-gpu-executed-a-blit.md) |
| `XY_COLOR_BLT` executes and writes memory | [i9xx_ring.c:223](../../src/chipsets/intel/i9xx_ring.c) | Phase 4 S09/S10, guards intact |
| Wrap/pad/free-space planning | `v9x_i9xx_ring_plan`, host-tested | `tests\host\test_i9xx_ring.c` |
| Submission and completion | [`v9x_d3d_i9xx_ring_submit`](../../src/display32/d3d/d3d_i9xx.c) | Runtime 3D since intel59 |
| Drain before CPU or display touches GPU memory | `v9x_render_drain` → `v9x_d3d_i9xx_render_drain` | [ddhal_core.c:1176](../../src/display32/ddhal_core.c) |
| Graphics address == framebuffer byte offset | plane base reads 0 at offset 0 | [i9xx_scanout.c:1](../../src/display32/engines/i9xx_scanout.c) header, already load-bearing for the flip |

And the DirectDraw core hands the ops table exactly the two things the Gen3
packet wants: `v9x_copy_rect_valid` returns the **surface base** byte offset
and leaves the rectangle surface-relative, bounded inside `fb.vram_bytes`.
That is the packet's `destination address` plus its `(x1,y1)-(x2,y2)`, with
no arithmetic in between.

## Deliverables

**New**

- `src\chipsets\intel\i9xx_blt.c` - `v9x_i9xx_build_color_blt2d` and
  `v9x_i9xx_build_src_copy_blt`, plus `v9x_i9xx_decode_blt_stream`, the
  independent allowlist. Pure C, no MMIO, host-testable.
- `tests\host\test_i9xx_blt.c` - golden dword streams, every refusal reason,
  the coordinate/pitch/bpp edges.
- `src\display32\engines\eng_i9xx.c` - `const V9X_ENGINE32_OPS
  v9x_engine32_i9xx`, seven members.

**Modified**

- `src\display32\d3d\d3d_i9xx.c` - one new export, a submit that seals the
  batch with a breadcrumb (design decision 5). The breadcrumb state stays
  private.
- `src\display32\ddhal_core.c` - one `case V9X_DD_ENGINE_TYPE_INTEL_GEN3` in
  `v9x_engine32()`.
- `src\display32\ddhal_internal.h` - the `extern` beside the two S3 tables.
- `scripts\build-ddraw-hal-dll.ps1`, `scripts\lib\host-sources.ps1`,
  `scripts\check-tree.ps1` file list.

**Deliberately unmodified**

- `v9x_i9xx_build_color_blt` in `i9xx_ring.c`. It is the Phase 4 diagnostic
  builder, it is 32-bpp-only with scratch bounds, and its output is rendered
  into `src\minivdd32\i9xx3d.inc` and CRC-checked by `check-tree.ps1`.
  Changing it changes an arm contract. The 2D builders are new code.
- The engine caps. `V9X_DD_ENGINE_CAP_SOLID_FILL` and `SCREEN_COPY` are read
  only by the 16-bit GDI layer ([gdi_accel.c:1199](../../src/display16/gdi_accel.c)),
  not by this path, which selects on `engine_type`. Publishing them would be
  a claim about GDI that is not true. They stay clear until Job B.
- The advertised DirectDraw caps. `DDCAPS_BLT` and `DDCAPS_BLTCOLORFILL` are
  already claimed for the whole binary and the CPU path already honours
  them. This plan changes who executes a blit, not what is advertised.

## Design decisions

1. **The packet encodings are transcribed, not reconstructed.** `XY_COLOR_BLT`
   is already in the tree; `XY_SRC_COPY_BLT`, the BR13 colour-depth field and
   ROP `CCh` come from §5 of the
   [Gen3 hardware audit](../decisions/2026-08-17-intel-gma-gen3-hardware-audit.md),
   which cites `gt/intel_gpu_commands.h`. The constants go in
   `include\velocity9x\intel_gma.h` beside `V9X_I9XX_XY_COLOR_BLT` with the
   source named, and each one is read off that header rather than derived
   from the existing literal's bit pattern.

2. **8 and 16 bpp only; 32 declines.** The audit names three depth encodings
   (8 bpp, RGB565, 32 bpp) and the family's four modes are 8 and 16 bpp, so a
   32-bpp path would ship untested on a mode this package cannot select. A
   555 primary declines too: the audit names no encoding for it and guessing
   one writes a whole framebuffer wrong. This mirrors `v9x_virge_copy`'s
   "unverified above 16 bpp, decline" and its reasoning.

3. **Address zero is accepted here, and refused by the 3D binder.** The
   depth/target binders refuse offset 0 because it is not theirs
   ([d3d_i9xx_target.c:153](../../src/display32/d3d/d3d_i9xx_target.c)). For a
   2D blit offset 0 is the primary and is exactly the surface a desktop blit
   aims at. The difference is deliberate and the test file says so, so a
   later reader does not "fix" one to match the other.

4. **The upper bound is `fb.vram_bytes`, strictly.** The ring and the status
   page live above it
   ([d3d_i9xx.c:354](../../src/display32/d3d/d3d_i9xx.c)); a blit that walked
   past the framebuffer would overwrite the ring it was submitted through.
   The core's rect validation already enforces this; the builder enforces it
   again on its own operands, because a builder that trusts its caller is not
   a check.

5. **Completion is the existing breadcrumb.** Each 2D batch is
   `XY_*_BLT`, `MI_FLUSH`, breadcrumb store, padded even. Arming the sequence
   is what makes `v9x_blt_drain`, `Lock` and `Flip` wait for a blit that has
   not landed - all three already call `v9x_d3d_i9xx_render_drain`, so
   sealing the batch is the whole of the synchronisation work. The new export
   exists because `v9x_d3d_i9xx_breadcrumb_expected` is static and should
   stay that way.

6. **`wait` maps onto the ring, not onto a FIFO.** `v9x_d3d_i9xx_ring_submit`
   already refuses a batch that does not fit rather than spinning for room.
   That refusal becomes `V9X_BLT_BUSY` → `DDERR_WASSTILLDRAWING` when the
   caller asked not to wait, and a drain-then-retry once when it did. A
   refusal for any other reason - depth, pitch, coordinates, decoder - is
   `V9X_BLT_DECLINED`, and the CPU path completes the blit with correct
   pixels, exactly as on the S3 engines.

7. **Overlapping copies decline.** `XY_SRC_COPY_BLT` carries no scan-direction
   control, which is the mechanism the ViRGE uses to make a same-surface
   overlapping copy correct. Nothing in the audit or the tree says what this
   blitter does when source and destination rectangles intersect. Build 002
   refuses when the two surface offsets are equal and the rectangles
   intersect; whether that can be lifted is its own step, with its own
   evidence.

8. **No new INI key and no new arm token.** The standing instruction of
   2026-09-18 is that the dev driver does not gate new mechanisms behind
   commands, and the ring-flip plan records it. The gate already in place is
   the right one: with no ring there is no `ring_linear_base`, the ops table's
   `ready` fails and every blit takes the CPU path. `V9X3D OFF` from DOS
   remains the one-line recovery, and it turns this off with everything else.

9. **There is no partial reset, and none is invented.** §5 of the audit says a
   wedged Gen3 ring is a full-GPU-reset event in the kernel driver, and
   `v9x_intel_recover` in `intel_backend.c` returns `V9X_STATUS_UNSUPPORTED`
   accordingly - so there is nothing here resembling the ViRGE's CR66 reset.
   The existing `hws_failed` and drain-abandon paths already stop asking the
   engine for things after it stops answering. This plan adds no poison latch
   of its own - a second, differently-shaped failure state on the same ring
   would only make a hang harder to read.

## Errata, assessed rather than inherited

The house rule from the [Phase 5 gate](../decisions/2026-09-15-intel-phase5-errata-gate.md)
is that a new class of workload does not inherit a previous class's
authorisation. This is a new class, and the assessment is short but not empty:

- **Erratum 12** is "an incorrect internal-buffer flush for a particular
  sequence of processor and integrated-graphics memory accesses", no published
  trigger, no silicon fix on 945GSE A3. A 2D blit is the Phase 4 *workload*
  (one blit, CPU polls head) but at an application's rate, aimed at memory an
  application is also writing through the aperture between blits. That is a
  denser interleave of CPU and GPU access to the same pages than the 3D path
  produces, and it is the erratum's own wording. Nothing measured says where
  the boundary is.
- **What is already authorised** goes most of the way: the sustained-3D
  amendment permits unbounded application-driven GPU work on this part, and
  the ring flip already has the GPU writing the plane base while the CPU
  draws the desktop. The increment here is which engine executes a copy the
  CPU was doing anyway - the accesses are not new, their source is.
- **Preconditions unchanged**: `8086:27AE` rev 03 exactly, scratch install,
  AC power, recoverable from DOS.

The decision this needs is one sentence: whether hardware 2D blits are
authorised on the same terms as runtime 3D. Recorded as a decision doc before
the first armed boot, not assumed by this plan.

## Rollout

Each build is independently testable and leaves the CPU path reachable.

| Build | Content | Exit gate |
|---|---|---|
| intel-2d-000 | Builders, decoder, host tests, ops table compiled and wired, every op returning `V9X_BLT_DECLINED` | `run-checks` green; a boot with the table present is indistinguishable from today - Final Reality frame rate, flip counters and `IntelLastResult` unchanged |
| intel-2d-001 | Solid fill (`XY_COLOR_BLT`) | Fill counter non-zero, guard words intact, the desktop survives a DirectDraw session; a bounded probe (below) reads the filled colour |
| intel-2d-002 | Screen-to-screen copy (`XY_SRC_COPY_BLT`), non-overlapping only | Ironfield BltFast frame rate at 16 bpp, before and after, on the netbook; declined-overlap counter accounts for what did not accelerate |
| intel-2d-003 | Overlap, if the behaviour can be established | A cited statement or a measurement, not an assumption. May close as "declines permanently" |

## Verification, and why the S3 harness does not transfer

The S3 `/accel` harness compares against a reference DC with `GetDIBits`.
That is a bulk read of the framebuffer through the aperture, which is the
access pattern measured to hard-lock this part
([bulk aperture reads](../decisions/2026-09-15-bulk-aperture-reads-hang-the-945gse.md):
153,600 dword reads hung it, three did not). Reusing it here would spend a
boot and lose the capture.

So the evidence is, in order of preference:

1. **Throughput, not pixels.** Ironfield's BltFast path is what the ViRGE
   blitter was measured on ([3→18 FPS](../decisions/2026-08-14-virge-blitter.md))
   and it needs no readback at all. Run it at 16 bpp - the 8-bpp desktop
   renders that application dithered, which is a known distraction and not a
   driver fault.
2. **Bounded probes.** The
   [bounded read-back plan](intel-phase5-bounded-readback.md) settled on
   fourteen named pixels as a conservative figure, explicitly not a
   proven-safe one. A fill and a copy are each checkable with a handful of
   corner and centre reads plus the guard words already in use.
3. **Counters.** Fills, copies, declines by reason, `BLT_BUSY` count, and
   whether any of them moved at all - the zero-counter trap the S3 harness
   fell into is already documented, so compare deltas across the run rather
   than against zero.
4. **The pipe CRC registers**, named in §6 of the hardware audit as the
   machine-checkable answer that costs no aperture reads. Unused so far; if a
   picture-level check is wanted later, this is the mechanism, and it is its
   own piece of work.

## What this will not have established

- Anything about GDI. The desktop is still drawn by the DIB engine into the
  aperture by the CPU.
- That the blitter is faster than the CPU here. The only Gen3 engine timing
  this project holds is Phase 4's floor - 16,382 NOOPs drained in 1 ms, blits
  completing before the first read-back. The netbook's CPU writes to WC
  aperture memory are slow enough that a win is expected, and expected is not
  measured.
- That overlap works, that 32 bpp works, or that a 555 primary can be
  expressed.
- That the framebuffer sits at GMADR offset 0 on a machine other than this
  one. The identity is measured on MICHAEL-NETBOOK and is already relied on
  by the flip; build 001 should record the plane base alongside the first
  blit so the assumption stays visible in the capture rather than becoming
  folklore.
