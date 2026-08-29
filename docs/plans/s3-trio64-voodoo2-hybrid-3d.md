# Direct3D rendering modes: one settings-page selector, four back ends

Status: planning, 2026-08-30. Nothing here is implemented.

The filename is historical. This document began as the S3 Trio64 + Voodoo2
hybrid architecture and that material is still here, intact, as **mode 4**.
What changed is that Voodoo2 offload turned out to be the last of four things
the driver should be able to do with Direct3D, not the only one, and planning
it first put the hardest and least useful mode at the front of the queue.

## The goal

One control on the Velocity9x page in Display Properties, choosing how
Direct3D is served:

| | Mode | What it does | Serves |
|---|---|---|---|
| 1 | **Disabled** | The driver advertises no Direct3D at all. DDRAW enumerates no hardware device and applications fall back to whatever else is present. | A machine with a second GPU; anyone whose game misbehaves through the narrow S3D path and wants it out of the way |
| 2 | **Software** | A CPU rasterizer behind the driver's own Direct3D HAL. Slow, correct, and available on every card the driver supports. | Trio32/64, ATI, generic VESA - every target that has no 3D today |
| 3 | **Hybrid** | The software rasterizer, with the 2D engine taking the work it is actually good at: clears, colour expansion, moving finished frames. | Trio64 and any other target with a blitter but no 3D |
| 4 | **Offload** | A Voodoo2 renders; windowed frames come back over PCI into the primary card's framebuffer, fullscreen uses the Voodoo2's own DAC through the VGA pass-through. | A period 2D + 3dfx pairing |

**Near-term scope is modes 1 and 2.** Mode 3 is a measurement away from being
plannable and mode 4 is a research project; both are recorded below so the
early work does not paint them out, and neither is being started.

The ViRGE's existing hardware path is not one of these four. It stays what it
is - the default on that card - and the selector chooses among alternatives to
it. Whether "hardware" becomes a fifth explicit entry rather than an implied
default is a UI question to settle when the page is built, not now.

------------------------------------------------------------------------

## What already exists

More than it looks like. Four of the five mechanisms this needs are in the
tree and exercised.

**The core/engine seam.** `src\display32\d3d\d3d_internal.h` defines
`V9X_D3D_ENGINE_OPS`: `limits`, `texture_format`, `describe_caps`,
`draw_triangles`. `d3d_core.c` holds everything chip-neutral - context pool,
texture handle table, render-state bookkeeping, software clipper, all sixteen
DDHAL entry points - and `d3d_virge.c` is the only file in the D3D path that
writes a hardware register. **A software rasterizer is a second
`V9X_D3D_ENGINE_OPS` and nothing else.** That split
([2026-08-29](../decisions/2026-08-29-d3d-core-engine-split.md)) was justified
on the grounds that a second engine was coming and would otherwise have to
move the seam. This is that second engine, arriving in software rather than as
3dfx.

`draw_triangles(context, vertices, triangle_count)` is a batch entry point on
purpose, and the reason was mode 4: the ViRGE is immediate-mode and would take
either granularity, but a command-stream engine needs a run of work to build
one packet from. A software rasterizer is happy with a batch too, so the rule
costs modes 2 and 3 nothing.

**A working "no Direct3D" implementation.** `dd16.c:490` gates on
`V9X_DD_ENGINE_CAP_D3D` and, when a family does not claim it, nulls
`GetDriverInfo`, `lpD3DGlobalDriverData`, `lpD3DHALCallbacks` and
`lpDDExeBufCallbacks`, and narrows `ddCaps` - on the DGROUP copy only, leaving
the shared block intact for the 32-bit side. The Trio64 and VBE families ship
that way today and DirectDraw is fully working on both. **Mode 1 is this
mechanism driven by a user setting instead of by a family manifest.**

**A settings mechanism the 16-bit driver already reads.** `gdi_accel.c:839-866`
reads `[Velocity9x]` out of `SYSTEM.INI` with `GetPrivateProfileInt`:
`GdiAccel`, `GdiAccelThreshold`, `GdiAccelFill`, `GdiAccelCopy`,
`GdiAccelOverlap`, `GdiAccelUpload`. Same section, same call, same enable-time
timing. A `Direct3D=` key needs no new plumbing, and the chip remains the
authority afterwards - `gdi_accel.c` masks the user's request against
`engine_caps` and the D3D selector must do the same.

**A settings page, and a DLL that already writes.** `V9XSETP.DLL` registers a
Display Properties page (`settings_propsheet.c`) and reports driver state
(`settings_status.c`). It is read-only today, but `settings_syncmodes.c`
already creates and prunes registry keys under the display class instance the
INF marked with this family's `V9xFamily` value, so writing a setting is a
known operation in a known place rather than a new capability.

**Validation.** `V9XDDP.EXE` pixel-verifies the Direct3D path and
`V9XTRACE.EXE` reads the driver's counters; the gate's three hygiene rules are
in [2026-08-29](../decisions/2026-08-29-d3d-core-engine-split.md), and
[the Final Reality runbook](../specifications/final-reality-101-runbook.md) is
the end-to-end check.

What does **not** exist: a rasterizer, and any way to change the published
capability set at run time. The second is the harder half.

------------------------------------------------------------------------

## The constraint that shapes all of this

**Direct3D capabilities are published at DriverInit, and DriverInit runs
before the 16-bit side has said anything about the hardware.**

This is measured, not theoretical. `v9x_d3d_publish_engine()` in
`d3d_core.c:1284` carries a long comment about it: selecting the engine at
publish time on `engine_type` published nothing at all, because at that point
`engine_type` is 0 and `engine.flags` carries no `V9X_DD_ENGINE_VALID`. On the
ViRGE guest `D3DHalFound` went 1 to 0 and every Direct3D pixel test vanished.
The function therefore returns the one engine the binary carries, and
`v9x_d3d_engine()` - the *draw-time* selector - is where chip selection
actually happens.

A mode selector has exactly this shape and will hit exactly this wall. The
consequence is a design rule, not a caution:

> **The mode must be decided on the 16-bit side and stamped into the shared
> block before `DriverInit` is called.** It cannot be read by the 32-bit HAL
> at publish time, because at publish time the HAL knows nothing.

That is the same fix the existing comment names for a second D3D engine
("stamp the chip's `engine_type` into the shared block before DriverInit"), so
modes and a second engine want the same piece of work. Do it once.

Two consequences follow:

- **A mode change needs a driver re-enable at minimum.** Whether DDRAW
  re-reads `DDHALINFO` on a live mode switch, or only on a fresh
  `DirectDrawCreate`, or only after a restart, is **not known** and must be
  probed before the page promises anything. The honest first version says
  "takes effect after restart" and is upgraded if a probe earns it.
- **A bad setting must not be able to strand the machine.** Mode 1 is safe by
  construction - it removes capability. Mode 2 is not: a rasterizer that
  faults takes DirectDraw applications down with it. The setting is read at
  enable time, so the recovery is the existing one - boot to a state where
  `SYSTEM.INI` can be edited - and `V9XFIX.BAT` should learn to clear the key.

------------------------------------------------------------------------

## Mode 1 - Disabled

The cheapest of the four and worth doing first for that reason alone: it
exercises the whole settings-to-shared-block-to-published-caps chain with a
back end that already works.

**Mechanism.** `enable16.c`/`dd16.c` reads `[Velocity9x] Direct3D` from
`SYSTEM.INI` at enable time. Value 0 clears `V9X_DD_ENGINE_CAP_D3D` from the
engine caps it stamps into the shared block, and the existing clamp at
`dd16.c:490` does the rest unchanged. No 32-bit change at all.

That the clamp is driven by `engine_caps` rather than a build-time define -
already true, and already commented as deliberate - is what makes this a
setting rather than a rewrite.

**Work**

1. Define the key and its values. One integer, defaults to the family's
   current behaviour so an absent key changes nothing.
2. Read it in the 16-bit enable path, mask `engine_caps`, stamp.
3. Report the effective mode on the settings page (read-only first).
4. Make the page write it.
5. Probe what a mode change actually requires before the page claims anything
   about when it takes effect.

**Gate.** `V9XDDP.EXE` on the ViRGE with the key set to 0 must report
`D3DHalFound=0` and `TexFormatCount=0` while every DirectDraw key stays green -
which is exactly the `Win98SE-Mach64VT2` result already recorded in the
changelog for a family with no D3D, so there is a known-good shape to compare
against. With the key absent or 1, the full existing gate must be unchanged,
pixels included.

**Open question.** A machine with a second GPU is the stated motivation, and
this driver has never been tested alongside one. Whether Windows 9x picks the
other card's Direct3D once this one stops advertising is unverified and is not
something mode 1 can promise on its own.

------------------------------------------------------------------------

## Mode 2 - Software rasterizer

The substantial one, and the one that changes what the driver is: Direct3D on
the Trio32/64, on ATI, and on generic VESA, none of which have any today.

### Why this is different from everything else in the project

**It is host-testable.** A scanline rasterizer is arithmetic over a buffer -
no MMIO, no FIFO, no guest. `CLAUDE.md`'s layering rule already asks for
exactly this shape ("hardware mechanics live behind a backend; policy lives in
host-testable C", with `src\common\mtrr.c` as the pattern), and this is the
first module in the tree that can be almost entirely policy. Triangle
traversal, edge stepping, depth compare, texture sampling, clipping and colour
interpolation can all be red-then-green under `build-host.ps1`.

That matters economically. Every other feature here costs a build, a deploy, a
reboot and a screenshot per iteration; this one costs a compile. The Z-buffer
work of 2026-08-30 spent most of its wall clock on guest round trips and found
its actual defect in a register field, which a host test could never have
caught - but a rasterizer inverts that ratio, and the plan should exploit it
rather than default to the probe-and-record loop.

The parts that are **not** host-testable are the boundaries: where the target
surface lives, what DDRAW hands over, and how fast writing to it is.

### The selector has to be inverted

`v9x_d3d_engine()` today resolves on `engine_type`, returns the ViRGE engine
for `S3_VIRGE_DX` and null for everything else. The VBE family declares
`EngineType = 'NONE'` and `EngineCaps = @()`; the Trio64 declares
`S3_TRIO64` with no `D3D` cap.

A software engine is selected by **mode**, not by chip - that is the whole
point of it. So the selector becomes: mode first, chip second. Mode 2 resolves
the software engine whatever the chip is, including when there is no engine
descriptor worth speaking of.

**Check before designing on it:** whether `engine.flags` carries
`V9X_DD_ENGINE_VALID` at all on a family whose `EngineType` is `NONE`.
`v9x_d3d_engine()` tests that flag before anything else, and if a tier-0
family never sets it, the software engine can never resolve there. This is a
five-minute read of `enable16.c` and it decides whether the VBE path is
reachable, which is half the value of mode 2.

### Where the pixels go, and the thing that will decide performance

DirectDraw hands the driver a render target that, on this driver, is in video
memory. **A CPU rasterizer writing into VRAM over the PCI aperture is
uncached, write-combining at best, and byte-for-byte the slowest place it
could put a pixel.** The obvious alternative - rasterize into system memory
and move the finished frame - is not the driver's choice to make on its own,
because the application created the surface.

This is not a detail to discover during implementation. It is the first
measurement mode 2 needs, and it is the same shape as the measurement mode 4
turns on. Benchmark a plain fill and a textured span into a video-memory
surface against the same into system memory, on the real guest, before writing
a rasterizer around either assumption.

It is also the seam where mode 3 attaches.

### Scope for a first version

Deliberately narrow, and narrow in the same places the ViRGE path is narrow so
the core does not have to grow new shapes:

- **16 bpp render target only.** `target_bits_per_pixel` is already a
  per-engine limit, the core's surface validation assumes RGB565 throughout,
  and the README already states Direct3D is 16-bpp only. Widening it is a
  separate change with its own evidence.
- Pre-transformed, pre-lit vertices only, as now.
- Triangle lists only, through the existing clipper and the existing
  `V9X_D3D_MAX_BATCH_TRIANGLES` / `V9X_D3D_MAX_FAN_TRIANGLES` bounds.
- Gouraud, one texture, point and bilinear sampling, 16-bit depth.
- **Advertise nothing that is not pixel-verified.** See below.

`V9X_D3D_ENGINE_LIMITS` is **append-only and positionally initialised** - C89
has no designated form and every member is an arithmetic type, so a field
inserted mid-struct silently reassigns every value after it with no diagnostic.
That is not hypothetical; it turned the clipper's guard band into sixteen
pixels and made all Direct3D rendering black while every HRESULT reported
success. The software engine supplies its own instance of that struct, which is
a second chance to make the same mistake.

### What must not be advertised

The characteristic failure of this project's Direct3D work is advertising a
capability and not implementing it. `D3DPRASTERCAPS_ZTEST`, all eight
`dwZCmpCaps` and `DDBD_16` were published for weeks while the engine wrote
`Z_BASE = 0`, set no depth bits and never read `context->zbuffer`. Applications
got depth accepted and ignored, and drew in submission order.

A four-mode selector multiplies the ways to make that mistake by four, because
each mode publishes a different capability set through its own
`describe_caps`. The discipline that caught it is already in the probe: a
pixel test per capability, keyed so "not attempted" and "attempted and wrong"
cannot look alike, and `/zprivate` shows how a probe switch selects between two
designs whose driver-side counters would otherwise be conflated. Mode 2 needs
the same: a probe switch that runs the full pixel ladder against the software
engine and reports which mode produced it.

**Final Reality cannot substitute for this.** Its `Visual appearance`
percentage reads the same value before and after hardware depth testing began
working, and the same again for its own built-in ViRGE reference entry - it
appears to score the advertised capability set rather than the rendered image.
A software rasterizer that advertises the same capabilities and draws garbage
would score identically. FR is an integration test, not a correctness one.

### Work order

1. Measure VRAM versus system-memory write cost on the guest.
2. Decide the mode plumbing (shared with mode 1) and land it.
3. Invert the engine selector; confirm the VBE reachability question above.
4. Rasterizer core, host-tested: edge setup, traversal, span fill, Gouraud
   interpolation. Red then green, per stage.
5. Depth: 16-bit, compare and write mask, host-tested against the same eight
   comparison functions the ViRGE path implements.
6. Texture sampling: point, then bilinear.
7. `describe_caps` publishing only what steps 4-6 verified.
8. Probe ladder against the software engine on a real guest.
9. Final Reality on a Trio64 guest - the first time that machine has run a
   3D benchmark at all.

Steps 4 to 7 need no guest.

------------------------------------------------------------------------

## Mode 3 - Hybrid, deferred

The idea: the software rasterizer, with the 2D engine doing the parts it is
good at. On a Trio64 that engine is the 8514/A block already driving
`V9X_DD_ENGINE_CAP_SOLID_FILL` and `V9X_DD_ENGINE_CAP_SCREEN_COPY`, and the S3
register header already carries the colour-expansion vocabulary
(`MONOSRCBLT`, `bSRC_Mono`, the CPU-source blits).

**The honest reading of where the wins are.** A triangle rasterizer's inner
loop is per-scanline spans, often a few dozen pixels. Issuing an 8514/A blit
per span means paying FIFO reservation and register setup per span, and on a
period part that will lose to a CPU loop for anything but very wide spans.
Assuming otherwise is the same class of mistake as assuming a FIFO could
supply 18 slots when it reports 16.

The wins that are plausible without measurement:

- **Clearing.** DirectDraw has no `DDBLT_DEPTHFILL` path in this driver and
  emulates depth clears on the CPU every frame. That is already measured as
  expensive: Final Reality's 25-pixel figure fell 28.54 to 23.62 Kpolys/s once
  depth testing was real, and the CPU depth clear is part of that cost. A
  hardware solid fill for the depth and colour clears is one blit per frame,
  not one per span, and it is the same work `DDBLT_DEPTHFILL` needs anyway.
- **Presenting.** Moving a finished frame from wherever it was rasterized into
  the visible surface is one large screen-to-screen copy - exactly what the
  blitter is for, and exactly the question mode 2's first measurement asks.
- **Colour expansion** for whatever ends up being blitted from a mask.

So mode 3's first deliverable is `DDBLT_DEPTHFILL`, which is already a
deferred item on the Final Reality plan, and which is worth doing whether or
not mode 3 ever exists. Everything past that waits on mode 2's numbers.

------------------------------------------------------------------------

## Mode 4 - Voodoo2 offload, deferred

The original subject of this document. The research below stands; what has
changed is its position in the queue and two notes from the D3D work since.

**Two notes first.** The batch granularity of `draw_triangles` was fixed for
this mode specifically - a command-stream engine needs a run of work to build
one packet from - so the seam is already the right shape and mode 4 does not
have to move it. And **no target is confirmed**: every other family in this
project has a VM or a physical machine behind it, and this one has neither
recorded. Establish whether a Voodoo2 can be emulated or is physically present
before planning against it, because a plan with no target is how this project
ends up with untestable branches.

### Goal

An S3 Trio64 as the normal Windows display and 2D accelerator, a Voodoo2 as a
3D coprocessor, with two presentation paths:

1. **Windowed:** the Voodoo2 renders, the finished image is read back over PCI
   and copied into the Trio64 framebuffer.
2. **Fullscreen:** the Voodoo2 takes the monitor using its analogue VGA
   pass-through switching, avoiding the copy entirely.

The physical pass-through cable is retained. Removing it would force
fullscreen through PCI readback as well, which adds bandwidth and latency for
no benefit.

### Confirmed Voodoo2 controls

| Function | Control | Purpose |
|---|---|---|
| VGA pass-through | `FBIINIT0` bit 0 | Whether the Trio64 signal passes through or the Voodoo2 owns the output |
| Enable framebuffer reads | `FBIINIT1` bit 3 (`EN_LFB_READ`) | Linear-framebuffer reads |
| Select framebuffer | `LFBMODE` bits 7:6 | Front, back or depth buffer for LFB access |
| Pixel access | Linear framebuffer | 16-bit pixels over PCI, two per 32-bit read |
| PCI read optimisation | `FBIINIT4` | LFB read-ahead and PCI read controls |

Linux `sstfb` switches pass-through as:

```c
if (passthrough)
    fbiInit0 &= ~DIS_VGA_PASSTHROUGH;
else
    fbiInit0 |= DIS_VGA_PASSTHROUGH;
```

It temporarily enables access to protected initialization registers through
PCI configuration register `0x40`.

References: the [Voodoo2 specification](https://people.freedesktop.org/~anholt/specs/3dfx/voodoo2.pdf),
[`sstfb.c`](https://codebrowser.dev/linux/linux/drivers/video/fbdev/sstfb.c.html)
and [`sstfb.h`](https://codebrowser.dev/linux/linux/include/video/sstfb.h.html).

### Presentation

```text
WINDOWED                          FULLSCREEN

Voodoo2 renders                   Voodoo2 renders
      |                                 |
LFB read over PCI                 Voodoo2 framebuffer
      |                                 |
Trio64 VRAM                       Voodoo2 RAMDAC
      |                                 |
Trio64 DAC                          Monitor
      |
Voodoo2 pass-through
      |
   Monitor
```

Windowed: `FBIINIT0.DIS_VGA_PASSTHROUGH = 0`, `FBIINIT1.EN_LFB_READ = 1`, and
`LFBMODE[7:6]` selects front (`00`) or back (`01`). The Voodoo2 need not
understand Windows clipping or window position - it renders its framebuffer,
and the driver treats that as an off-screen surface and composites the required
rectangle.

Fullscreen: wait for idle, configure video timing and framebuffer, enable
Voodoo2 output, then `DIS_VGA_PASSTHROUGH = 1`. The Trio64 stays configured
with the desktop resident in its VRAM. Exit or Alt-Tab reverses it.

### Frame synchronisation

Voodoo2 LFB reads are not asynchronous GPU readback; they interact with the
pipeline and can force synchronization, so reading while rendering can perform
badly. The first implementation should be deliberately conservative: render
frame N, finish, wait for idle, read the rectangle, copy into Trio64 VRAM,
render N+1. Command scheduling and buffering come after that works.

### PCI bandwidth

Payload only, 16-bit framebuffer:

| Render size | Bytes/frame | 30 FPS | 60 FPS |
|---|---|---|---|
| 320x240 | 153,600 | 4.6 MB/s | 9.2 MB/s |
| 400x300 | 240,000 | 7.2 MB/s | 14.4 MB/s |
| 512x384 | 393,216 | 11.8 MB/s | 23.6 MB/s |
| 640x480 | 614,400 | 18.4 MB/s | 36.9 MB/s |

Real traffic is worse: transaction overhead, chipset behaviour,
synchronization, and the subsequent write into Trio64 VRAM - which is the same
uncached-write cost mode 2 has to measure anyway. So 320x240 windowed is very
promising, 400x300 promising, 512x384 needs measurement, 640x480 depends
strongly on readback performance, and fullscreen has no readback problem at
all. Dirty-rectangle copying could cut traffic substantially.

`FBIINIT4`'s read-ahead and PCI read options should be benchmarked per chipset
rather than assumed to help.

### Proof of concept, before any Direct3D

A `V2HYBRID.VXD` plus a `V2TEST.EXE`: enumerate PCI, find the Voodoo2, map its
BARs, enable protected init-register access, configure LFB reads, control the
pass-through mux, expose a framebuffer read. Then, in order:

- **Test A, VGA mux.** Prove software can reliably change display ownership,
  repeatedly, idle and after rendering, with restoration after termination and
  after a crash where practical. A safe recovery path matters - a wrong mux
  state leaves a blank display.
- **Test B, readback.** Render a deterministic pattern, read it through the LFB
  aperture, and validate pixel order, RGB565/555 interpretation, scanline
  orientation, stride, buffer selection, clipping and offsets. Then copy it to
  the Trio64.
- **Test C, bandwidth.** Sequential read performance at 320x240, 400x300,
  512x384 and 640x480: MB/s, ms/frame, theoretical maximum FPS, CPU
  utilization, and the effect of read-ahead and the PCI read options. **This
  is the gating measurement for the whole mode.** At 25 MB/s a 640x480x16
  frame is 614,400 bytes, about 40 complete reads per second - which is not
  40 FPS, because rendering, synchronization and the Trio64 copy still have to
  happen.
- **Test D, windowed presentation.** Desktop at 640x480x16 or 800x600x16, 3D
  window at 320x240x16, both 16-bit so no colour conversion is needed. Window
  movement, clipping, partial obscuring.
- **Test E, fullscreen switching.** Clean transitions both ways under the same
  driver.

Only then a HAL translating render target, texture state and coordinates,
Z state, blending, fog, Gouraud, clipping, viewport and triangle setup - using
Glide and open-source 3dfx code as reference rather than re-deriving the
hardware programming.

------------------------------------------------------------------------

## Traps already paid for

Every one of these was paid for on this driver, most of them within the last
two days. They apply to modes 1 and 2 as much as to anything.

**Advertising is not implementing.** Depth testing was published complete and
did nothing for weeks, with every HRESULT reporting success. Four modes means
four capability sets, each of which can lie independently.

**Publish time is not draw time.** Caps go out at DriverInit, before the
16-bit side has described the hardware. Selecting on anything the 16-bit side
provides, at publish time, publishes nothing - measured, `D3DHalFound` 1 to 0.

**Positional initialisers over a struct of arithmetic types.**
`V9X_D3D_ENGINE_LIMITS` is append-only for a reason that cost a four-bisect
investigation pointing at the headers.

**Register fields have widths.** The depth path asked for 18 FIFO slots from a
five-bit free-slot field that reports 16, and every depth-enabled draw timed
out and reset the engine while returning `S_OK`. Read the field, do not assume
the count.

**The gate's hygiene rules are load-bearing.** Delete the result file before
the run, check the `exec` exit code, and require the `Build=` key to match.
Two of those three were once absent and a stale file passed as a result.

**Pick the control by what it does, not where it sits.** A "known-good"
baseline built from the commit before a fix is a known-*broken* baseline.

**`V9XTRACE.EXE` faults in KRNL386 once DirectDraw has run on the boot**
([issue](../issues/2026-08-30-trace-dump-krnl386-flush-gpf.md)). Take the trace
after the run, never before - a 16-bit GPF dialog wedges the agent and costs
whatever was in progress. The scalar counters are written before the stages
that fault.

**The shared block has room, but the ABI is a contract.** `V9X_DD_SHARED` is
3122 bytes of the 4096 the 16-bit side allocates. Adding a mode field is cheap;
adding it means bumping `V9X_DD_SHARED_ABI` and rebuilding the 16-bit driver,
the HAL and the diagnostic tools together, and the compile-time size assertions
have to be updated with it.

------------------------------------------------------------------------

## Development order

Modes 1 and 2 only. Mode 3 begins at `DDBLT_DEPTHFILL`; mode 4 begins at
confirming a target exists.

1. Probe what changing a Direct3D capability set at run time actually requires:
   re-enable, `DirectDrawCreate`, or restart.
2. Check whether `V9X_DD_ENGINE_VALID` is set on a family whose `EngineType` is
   `NONE`, which decides whether mode 2 can reach the VBE path.
3. Measure CPU write cost into a video-memory surface against system memory.
4. Mode plumbing: `[Velocity9x] Direct3D` in `SYSTEM.INI`, read at enable time,
   stamped into the shared block before DriverInit, with the ABI bump.
5. **Mode 1** end to end, gated against the existing no-D3D family shape.
6. Settings page reports the effective mode, then writes it.
7. Rasterizer core, host-tested, red then green: traversal, spans, Gouraud.
8. Depth compare and write mask, host-tested, all eight functions.
9. Texture sampling, point then bilinear.
10. `describe_caps` publishing only what 7-9 verified.
11. **Mode 2** probe ladder on a guest, per mode.
12. Final Reality on a Trio64 guest.

The measurement in step 3 is the one that can change the shape of mode 2, and
step 1 is the one that can change the shape of the settings page. Both are
cheap and both come before any rasterizer is written.
