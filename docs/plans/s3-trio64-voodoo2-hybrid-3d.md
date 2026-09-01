# Direct3D rendering modes: one settings-page selector, four back ends

Status: 2026-09-01. **Mode 1 is written, gated and measured on the ViRGE
guest** - see [the gate record](../decisions/2026-08-30-d3d-mode-disabled-gate.md).
**Mode 2 rasterizes depth-tested Gouraud triangles**, measured on a Trio64 -
see [the rasterizer record](../decisions/2026-09-01-software-rasterizer-depth.md).
Modes 3 and 4 are planning only.

Reviewed against the tree the same day: two open questions closed by reading
code, one target named, the development order re-cut around what that changed,
and mode 1 landed. Every claim added by that review is either a file:line
citation or an existing decision record - none of it was measured.

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

**Where this stands, 2026-09-01.** **Mode 1 is done, released in 0.6.5 and
measured on three chips.** **Mode 2 draws depth-tested Gouraud triangles on a
Trio64** - work-order steps 3 to 7, measured on the guest against the ViRGE as
a control ([record](../decisions/2026-09-01-software-rasterizer-depth.md)). It
has no texture sampling, and it advertises neither depth nor texturing, so no
application that checks caps will use what works. **Mode 3 is the current
direction**, its first deliverable
(`DDBLT_DEPTHFILL`) landed, and it is now blocked on an instrument rather than
on a design: see its section. Mode 4 remains a research project with no
confirmed target.

That ordering changed from "near-term scope is modes 1 and 2" and the reason is
worth keeping: mode 3 was described here as "a measurement away from being
plannable", and doing its first deliverable proved the measurement is the hard
part. The instrument, not the rasterizer, is what the next work builds.

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
writes a hardware register. That split
([2026-08-29](../decisions/2026-08-29-d3d-core-engine-split.md)) was justified
on the grounds that a second engine was coming and would otherwise have to
move the seam. This is that second engine, arriving in software rather than as
3dfx.

> **Correction, 2026-08-30.** This section used to say "a software rasterizer
> is a second `V9X_D3D_ENGINE_OPS` and nothing else". That is wrong, and it is
> wrong in the direction that costs a session: **three chip-specific gates sit
> in the chip-neutral draw path**, and on any card but the ViRGE they would let
> a software engine resolve, publish caps, accept every call and draw nothing,
> with every HRESULT reporting success. Read before writing a rasterizer:
>
> 1. `v9x_d3d_engine()` (`d3d_core.c:50`) requires `V9X_DD_ENGINE_VALID`, which
>    only a chip with a `fill_engine_descriptor` hook sets - so the VBE, both
>    ATI and the Matrox devices never get past it. Already recorded under mode
>    2 below.
> 2. `v9x_engine_status_validated()` gates **all three** Direct3D draw entry
>    points (`d3d_core.c:1040`, `:1143`, `:1178`). It lives in
>    `engines\eng_s3_virge.c` and resolves through `v9x_engine_ready()`, which
>    tests `engine_type == V9X_DD_ENGINE_TYPE_S3_VIRGE_DX` **literally**, plus
>    a non-zero control window and a mapped MMIO aperture. A Trio64 fails it
>    even though it sets `V9X_DD_ENGINE_VALID`.
> 3. `v9x_engine_validate_status()` is called unconditionally before each of
>    those three draws and pokes ViRGE MMIO to do it.
>
> All three are 2D-engine concepts that predate the D3D split and were correct
> while one chip had the only D3D path. A software engine is subject to none of
> them: it needs no MMIO window, no FIFO and no status register. They have to
> become engine-vtable questions - "is this engine ready to draw" asked of the
> `V9X_D3D_ENGINE_OPS`, not of the ViRGE - before any rasterizer output can be
> believed.
>
> This is the same shape as the publish-time defect recorded below: the failure
> mode is silence, not an error, and it would read as a bug in the rasterizer.

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

**And there is already a place to do it.** Reading the escape sequence in
`dd16.c`: DDRAW's first `DDCREATEDRIVEROBJECT` returns 0 because
`v9x_dd_set_info` is still null (`dd16.c:434`); DDRAW then issues
`DDGET32BITDRIVERNAME`, which calls `v9x_dd_block()` - allocating and zeroing
the shared block, stamping `dwSize`, `abi` and the mode table
(`dd16.c:255-257`) - and hands back `V9xDdSharedLinear()` as the context
DriverInit is called with. Only `v9x_dd_refresh_framebuffer()`, which is where
the engine descriptor is filled (`dd16.c:312-341`), runs later, on the retry.

So the shared block is live and writable before DriverInit, and
`v9x_hw16_active_device()` already resolves by then - the PCI scan ran at
Enable, long before any of this. Both the mode and `engine_type` can be
stamped in `v9x_dd_block()`. The mode has no ordering dependency on the
hardware at all; it comes out of `SYSTEM.INI`.

This is a code-reading result, not a measured one. It says the change is small
and says where it goes; it does not say DDRAW behaves as expected once the
published caps differ.

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

Status: **complete and measured on `Win86SE`**, all five steps. The gate record
is [2026-08-30](../decisions/2026-08-30-d3d-mode-disabled-gate.md).

On a fresh boot with the key set, `D3DHalFound=0`, `TexFormatCount=0` and
`D3DDeviceCount=3` - Ramp, RGB and MMX, with no `Direct3D HAL` entry - while
DirectDraw stays fully working. Setting the key back reproduced the baseline
ladder with **no differing `D3D*` or `Tex*` key at all**.

**Step 5 is answered, and the answer constrains the page.** A re-enable stops
the driver serving Direct3D but does not stop DDRAW offering the device: the
HAL entry stays enumerated from the previous session's `DDHALINFO` and
`CreateDevice` then fails with `E_NOINTERFACE`, which is worse for an
application than either end state. Only a restart removes it. So the page says
"takes effect after restart", measured rather than assumed.

The cheapest of the four and worth doing first for that reason alone: it
exercises the whole settings-to-shared-block-to-published-caps chain with a
back end that already works.

**Mechanism.** `dd16.c` reads `[Velocity9x] Direct3D` from `SYSTEM.INI` at
enable time. A mode that advertises nothing clears `V9X_DD_ENGINE_CAP_D3D` from
the engine caps it stamps into the shared block, and the existing clamp in
`V9xDdCreateDriverObject` does the rest unchanged. No 32-bit change at all,
and no ABI change.

That the clamp is driven by `engine_caps` rather than a build-time define -
already true, and already commented as deliberate - is what makes this a
setting rather than a rewrite.

**The values**, in `include\velocity9x\d3dmode.h`, are this document's mode
numbers with zero added for the chip's own engine:

| | | |
|---|---|---|
| 0 | hardware | the chip's engine if it has one. The default, and what an absent, blank or unparsable key means |
| 1 | disabled | mode 1 |
| 2 | software | mode 2, recognised and not implemented |
| 3 | hybrid | mode 3, recognised and not implemented |
| 4 | offload | mode 4, recognised and not implemented |

An unrecognised number is a typo, not a request, and reads as 0.

A recognised-but-unimplemented mode advertises nothing rather than falling back
to hardware. Falling back would answer "software" with the ViRGE's S3D engine,
which is the same class of defect as advertising a capability that is not
implemented - and this driver has shipped that once already.

**Where the policy lives.** `src\common\d3dmode.c`, pure and host-tested, with
`dd16.c` reading the key and applying the answer. The split is
`CLAUDE.md`'s layering rule, and it buys a specific thing here: the property
that no setting can grant a capability the chip does not have is asserted over
the whole 16-bit request space in `tests\host\test_d3dmode.c`, rather than
being a comment beside an `if`.

**Recovery is not needed for this mode**, and the plan's earlier note that
`V9XFIX.BAT` should learn to clear the key belongs with mode 2 rather than
here. Mode 1 only removes capability: DirectDraw stays fully working, GDI is
untouched, and the worst a bad value can do is read as 0 and change nothing.
Nothing can strand a machine until a rasterizer is running in the HAL.

**Work**

1. ~~Define the key and its values.~~ Done.
2. ~~Read it in the 16-bit enable path, mask `engine_caps`, stamp.~~ Done.
3. ~~Report the effective mode on the settings page (read-only first).~~ Done,
   as `Direct3DMode=` in `V9XHW.INI`, separate from the chip module's existing
   `Direct3D=`: the page has to tell "this card has none" apart from "you
   turned it off", and one key cannot say both.
4. ~~Make the page write it.~~ Done. The Direct3D row is a combo box carrying
   only the modes this build implements - offering "Software" before it
   renders a pixel would be the UI form of advertising an unimplemented
   capability - with `Apply` greyed until the selection differs from the file,
   so opening the page and pressing `OK` writes nothing. The whole loop was
   driven on the guest at 1024x768 and again at 640x480, where the height
   budget is binding.
5. ~~Probe what a mode change actually requires before the page claims
   anything about when it takes effect.~~ Done. Restart.

**Gate, and it passed.** `V9XDDP.EXE` on the ViRGE with the key set to 1 must
report `D3DHalFound=0` and `TexFormatCount=0` while every DirectDraw key stays
green - which is the `Win98SE-Mach64VT2` shape already recorded for a family
with no D3D, so there was a known-good result to compare against. With the key
absent or 0, the full existing gate must be unchanged, pixels included.

Both held. The disabled runs also reported `D3DDeviceCount=3` - Ramp, RGB and
MMX, no `Direct3D HAL` entry - and the restored run differed from the baseline
in no functional key at all. (This paragraph previously had the two values the
wrong way round, written before the encoding was settled: 0 is hardware, 1 is
disabled.)

**Open question.** A machine with a second GPU is the stated motivation, and
this driver has never been tested alongside one. Whether Windows 9x picks the
other card's Direct3D once this one stops advertising is unverified and is not
something mode 1 can promise on its own.

------------------------------------------------------------------------

## Mode 2 - Software rasterizer

Status: **steps 3, 4 and 5 are done and measured. Step 6 - the rasterizer
itself - is where the next work starts.**

A Trio64, which has no 3D engine, now enumerates a Direct3D HAL device,
creates it and puts pixels on the screen under `Direct3D=2`
([decision](../decisions/2026-08-30-software-d3d-path-proven.md)). The three
ViRGE gates named in the correction above are out of the chip-neutral path and
behind an appended `ready` hook, the mode reaches the 32-bit side as
`V9X_DD_ENGINE_CAP_D3D_SOFTWARE` with no ABI change, and the ViRGE's own ladder
is unchanged by the move.

What exists is not a rasterizer: `draw_triangles` fills each triangle's
bounding box with a flat colour, and `describe_caps` advertises nothing else.
That is step 5 doing its job - everything the rasterizer would otherwise have
had to debug at the same time is now known to work.

The substantial one, and the one that changes what the driver is: Direct3D on
the Trio32/64, on ATI, and on generic VESA, none of which have any today.

### The target, and what it can be expected to do

**BARRY** - `10.0.1.47:9869`, S3 Trio32/64 86C764, 2 MB, Windows 98 SE, a
pre-MMX classic Pentium with 32 MB
([baseline](../decisions/2026-08-27-crystalmark-barry-baseline.md)). It is the
only physical S3 target with DirectX, so it is where mode 2 gets judged.

Three consequences, and they should be stated before any expectation is set:

- **No MMX and no MTRRs.** Neither of the two levers that normally make a
  software rasterizer tolerable is available here.
- **2 MB of VRAM.** At 800x600x16 the desktop is 960 KB; a 640x480x16 render
  target plus a 16-bit depth buffer is another 1.2 MB and does not fit
  alongside it. 640x480x16 desktop (614 KB) plus a 320x240 target and depth
  (307 KB) does, with room. The mode matrix mode 2 is tested in has to be
  chosen against the card, not assumed.
### What mode 2 is for, and in which order

All three of compatibility, reach and speed are goals; they are not all
achievable on the same machine, and which apply is a property of the CPU. The
order is settled (2026-08-30):

1. **Compatibility first.** A title that refuses to start without a HAL device
   starts, and draws correctly. This is the goal on BARRY, and on this machine
   a textured, depth-tested rasterizer will be slow whatever is done to it -
   any frame-rate claim here is a guess until measured, and the honest
   expectation is low. Done means the pixel ladder, not a number.
2. **Reach**, once correctness holds: the ATI, Matrox and tier-0 families get a
   HAL where they have none. That is what the inverted selector above buys, and
   most of those targets are 86Box guests where a speed measurement would be
   the emulator's rather than the driver's.
3. **Speed, where the CPU allows it.** MMX and a write-combining aperture are
   the two levers, and BARRY has neither. A machine that has them can take a
   faster path; a machine that does not must not pay for one it cannot use. So
   any such path is selected from what the CPU reports, the way
   `include\velocity9x\mtrr.h` already gates on a capability ladder, rather
   than compiled in - and it comes after 1 and 2, not instead of them.

The `vbe` and ATI families widen the reach, but every one of those targets is
either an 86Box guest (where the measurement is the emulator's) or, for the
netbook and SOLO2150, a machine whose own reason for existing is elsewhere.

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

**Answered, 2026-08-30, by reading the tree: it does not.** `V9X_DD_ENGINE_VALID`
is set in exactly one place, `dd16.c:334`, and only when the active chip
supplies a `fill_engine_descriptor` hook; the `else` branch at `dd16.c:340`
sets `flags = 0`. That hook is NULL on
`v9x_vbe_device` (`vbe_hw16.c:62`), `v9x_mach64_vt2_device` (`vt2_hw16.c:20`),
`v9x_rage_mobility_device` (`mobility_hw16.c:26`) and `v9x_mga2_device`
(`mga2_hw16.c:41`). So on every family mode 2 is aimed at except the Trio64,
`engine.flags` is zero, and `v9x_d3d_engine()` returns null at
`d3d_core.c:50` before it looks at anything else.

Two ways out, and they are not equivalent:

- **Test the mode before the VALID flag in `v9x_d3d_engine()`.** The mode is a
  software setting, not a property of an engine descriptor, so it has no
  business behind a flag that means "a chip filled the descriptor in". Touches
  one function and no family.
- **Make every family stamp `VALID` with an explicit `TYPE_NONE` descriptor.**
  More honest about what the flag names, but it changes the flag's meaning for
  the 2D readers too - `v9x_engine32()` at `ddhal_core.c:723` and
  `v9x_can_set_display_start()` at `ddhal_core.c:330` both gate on it, and both
  currently short-circuit on tier-0 for free. Changing that is a behaviour
  change on four families to serve a D3D question.

Take the first. Note it inverts the selector in the order the section title
already asks for: mode first, chip second.

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

### What the tree already says about that measurement

Not enough to skip it, but enough to say which way it will fall and to stop
the rasterizer being designed around the wrong answer.

- **A byte-at-a-time loop over the aperture is unusable.** `blt_cpu.c:19-26`
  records 640x480x16 - 614,400 bytes - at roughly 700 ms per frame, about
  1 FPS, before the dword-at-a-time path replaced it. That is the cost of a
  naive span writer, measured, on the presentation path.
- **On an emulated guest the gap is small.**
  [2026-08-17](../decisions/2026-08-17-native-driver-benchmark.md) has Ironfield
  on tier-0 Mach64 at 31 FPS writing the backbuffer directly in VRAM against
  39 FPS through the HEL's system-memory copy. An 86Box framebuffer is host
  RAM, so that ratio measures the emulator, not a PCI aperture, and a mode 2
  measurement taken only in a VM will read as "VRAM is fine" and be wrong.
- **The obvious fix is unavailable on the target.**
  [MTRR stage A](../decisions/2026-08-28-mtrr-stage-a-inspect-only.md) found
  BARRY is a pre-MMX classic Pentium: no MTRRs, so its aperture cannot be made
  write-combining, and no MMX either, so the rasterizer's inner loop gets no
  help there. 86Box emulates no MTRRs on any CPU. Of the whole fleet only the
  netbook and SOLO2150 can take a WC range, and neither is a mode 2 target.

So the likely shape is: rasterize into system memory, present with one bulk
copy. That is not free either - **`v9x_cpu_copy()` computes both its source and
its destination from `v9x_hal->fb.linear_base` (`blt_cpu.c:83`), so it can only
copy VRAM to VRAM.** A system-memory render target needs a
system-to-video path that does not exist yet, and needs the driver to have a
say in where DDRAW puts the surface at all. **That, not the rasterizer, is
mode 2's first real piece of work**, and it should be settled before step 4 of
the work order rather than discovered inside it.

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

1. Measure VRAM versus system-memory write cost, **on BARRY**. An 86Box number
   here answers a different question; see above.
2. Settle where the render target lives, and whether the driver can decide it.
   If the answer is system memory, a system-to-video presentation path is part
   of mode 2 and `v9x_cpu_copy()` cannot serve it as written.
3. ~~Decide the mode plumbing (shared with mode 1) and land it.~~ **Done** -
   mode 1 landed it, and `Direct3D=2` already resolves to
   `mode-unimplemented` and advertises nothing, measured on the Trio64.
4. ~~Move all three chip gates behind the engine vtable~~ **Done.** The selector's
   `V9X_DD_ENGINE_VALID` test, `v9x_engine_status_validated()` on the three
   draw entry points, and the unconditional `v9x_engine_validate_status()`
   beside them. This is the whole of the core change, and the correction at
   the top of this document is why it is one step rather than the half-step
   the old wording implied.
5. ~~**Prove the path before writing any maths.**~~ **Done, 2026-08-30.** A stub engine whose
   `draw_triangles` fills the triangle's bounding box with a flat colour, wired
   in behind `Direct3D=2`, and run on the Trio64 guest. If a coloured rectangle
   appears where a triangle was asked for, then the selector, the three gates
   above, caps publication, context creation and the draw call all work on a
   card with no 3D engine - and every later failure is the rasterizer's.
   Without this the first rasterizer doubles as the first test of six other
   things, which is how the depth path lost two weeks.
6. ~~Rasterizer core, host-tested: edge setup, traversal, span fill, Gouraud
   interpolation.~~ **Done, 2026-09-01.** `src\display32\d3d\d3d_raster.c`,
   held by `tests\host\test_d3d_raster.c`. It writes into a caller-supplied
   surface - pointer, pitch, width, height - so it is pure arithmetic and does
   not care whether that memory is VRAM or a system-memory shadow. That keeps
   step 1's measurement out of the rasterizer's design entirely: it decides
   which pointer is passed, not how anything is written.

   Three decisions in it are worth carrying forward. **It is integer-only**,
   in 28.4 screen coordinates: the float-to-fixed conversion stays in
   `d3d_soft.c`, because it needs the `#pragma aux` fistp that keeps
   `d3d_zfixed.c` out of the MSVC host pass, and keeping it out of the
   arithmetic leaves the arithmetic portable and tested twice. **The target
   dimension is capped at 2048** and that cap is an overflow bound rather than
   a taste - every interpolation product is bounded by coordinate squared, and
   32752 squared is 1,072,693,504, so a 4096-pixel target would break every
   span silently and on large modes only. `d3d_soft.c` asserts its own
   `target_dimension_max` against it at compile time. **Coverage is pixel
   centres with half-open intervals in both axes**, which is what the test
   suite's central property checks: two triangles sharing an edge cover the
   pixels along it exactly once, neither twice nor not at all.

   What it is not: no depth, no texture, no fill-rule for anything but a
   triangle list, and no guest has drawn through it. The tests are properties
   - coverage, containment, flat colour staying flat, vertex-order
   independence - not a picture.
7. ~~Depth: 16-bit, compare and write mask, host-tested against the same eight
   comparison functions the ViRGE path implements.~~ **Done, 2026-09-01**, and
   measured on the Trio64 guest as well as host-tested.

   The comparison constants are numbered as `D3DCMP_*` numbers them, so the
   engine hands `context->z_func` over untranslated and `d3d_soft.c` asserts
   the equality at compile time. The ViRGE cannot do that - the S3D encoding
   differs from `D3DCMP - 1` in six of the eight - and the difference between
   the two is worth keeping in view, because a driver that silently used the
   wrong order draws a scene that is inside out rather than one that is
   missing. The gate for that is the host test's table, which asserts each of
   the eight from both sides.

   The depth gate copies the ViRGE's `z_active` verbatim: render state **and**
   an attached Z surface **and** a non-zero depth pitch. An application may
   legally set `ZENABLE` with no Z buffer bound and the runtime replays whole
   default state blocks, so acting on the render state alone points the depth
   unit at `depth_offset` 0, which is the visible framebuffer.

   Depth is also what sets the interpolator's headroom: the edge lerp forms
   `max(from, to) * denominator`, and 65535 against a 32752-subpixel y-span is
   2,146,631,520 - 852,127 short of overflowing. That margin is the real reason
   `V9X_D3D_RASTER_DIMENSION_MAX` is 2048, and there is a host test that draws
   the worst case rather than arguing about it.

   Both probe depth ladders pass rung for rung on the Trio64 while
   `D3DZCompareOk` and `D3DZWriteMaskOk` still read 0, because those keys fold
   in a colour comparison against ZRGB1555 constants that the **ViRGE's** own
   format defect put there. See the record; the probe's expectations are now
   hiding three passes rather than one, and fixing that is a decision about a
   validated baseline rather than a tidy-up.
8. Texture sampling: point, then bilinear.
9. `describe_caps` publishing only what steps 6-8 verified.
10. Probe ladder against the software engine on a real guest. **Partly done**:
    steps 6 and 7 were each run on `Win98SE-Trio64` with `Win86SE` as a
    hardware control, and two keys were added to the probe -
    `D3DTriangleShapeOk`, which is what separates a rasterizer from the stage-1
    bounding-box stub, and the four `D3DEdge*Raw` keys, which found that the
    core's clipper loses the last row and column on **both** engines
    ([issue](../issues/2026-09-01-clipper-loses-last-row-and-column.md)). The
    full ladder still needs re-running once step 9 publishes caps.
11. Final Reality on BARRY - the first time that machine has run a 3D
    benchmark at all. An integration check, not a correctness one; see the
    note above about what its score actually measures.

Steps 6 to 9 need no guest. Steps 3 and 4 are the only ones that touch the
chip-neutral core, and step 5 is what says they were done right.

------------------------------------------------------------------------

## Mode 3 - Hybrid

Status: **the current direction, 2026-08-30, and blocked on measurement rather
than on code.**

`DDBLT_DEPTHFILL` was mode 3's first deliverable and it is done
([decision](../decisions/2026-08-30-ddblt-depthfill.md)). What it produced was
not a win but an argument: the same change gains 22-36% on two scenes, loses
38% on a third, leaves the composite flat, and **nothing available could say
why**. Final Reality returns four composite numbers; it cannot separate "the
clear is slow" from "the clear interferes with queued 3D work", and answering
even that much took three full benchmark runs, a hand-built control HAL and a
callback-counting trick.

Every remaining question in this section is of that shape. Is a per-span blit
cheaper than a CPU loop, and above what span width? Does colour expansion pay?
Does a screen-to-screen present beat the CPU? Those are per-operation cost
questions, and the section below already warns that assuming an answer is
"the same class of mistake as assuming a FIFO could supply 18 slots when it
reports 16".

**So mode 3's real blocker is an instrument, not a design.**
`C:\everything\dispbench` is that instrument and is at stage 1 of 7; its
stage 2 and 3 are exactly the per-operation timing harness this needs. The
scoping for that, and for the survey/crowdsourcing expansion proposed alongside
it, is in [`dispbench-as-the-measurement-instrument.md`](dispbench-as-the-measurement-instrument.md).

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

**`DDBLT_DEPTHFILL` is done and pixel-verified**
([2026-08-30](../decisions/2026-08-30-ddblt-depthfill.md)), and it cost no
engine code: the ViRGE solid fill was already parameterised on offset, pitch
and pixel width, so the whole change is one validating body in `ddhal_core.c`
plus the cap. It is the first evidence for this section's thesis, and it is
the easy half of it - one blit per frame against one CPU pass per frame is a
structural win that needed no measurement to argue for. The per-span claims
below still do, and are still the ones this section warns against assuming.

What it does **not** yet show: whether the engine or the CPU fallback served
it - both write the same words - and whether Final Reality's polygon rate
moves. Neither blocks the rest of mode 3, and both are cheap to close.

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

**The gate's hygiene rules are load-bearing, and there are four.** Delete the
result file before the run, check the `exec` exit code, require the `Build=`
key to match - and **run the probe once per boot**. Two of the first three were
once absent and a stale file passed as a result. The fourth was added
2026-08-30: a second `V9XDDP.EXE` in one boot passes all three of the others
and still reports `D3DZWriteMaskOk`, `D3DDepthFogOk` and
`D3DVertexAlphaBlendOk` as failures against a run that had just passed.

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

The **layout** need not move, though. `V9X_DD_ENGINE.reserved1`
(`win9x_ddraw_abi.h:1077`) is a spare DWORD, and there is precedent for
claiming one: `fault_inject` took what was `reserved0` and the comment beside
it records that the layout was unchanged. Bump the ABI stamp for the semantic
change, keep every offset where it is.

**Mode 1 needs neither.** Clearing `V9X_DD_ENGINE_CAP_D3D` before the stamp
reuses a field that already exists, drives a clamp that already runs, and
changes nothing on the 32-bit side. Only mode 2 needs the mode itself visible
to the HAL, because only mode 2 has to select an engine on it.

------------------------------------------------------------------------

## Development order

Modes 1 and 2 only. Mode 3 begins at `DDBLT_DEPTHFILL`; mode 4 begins at
confirming a target exists.

Two of the three questions this list used to open with have been answered by
reading the tree, and are struck out here rather than deleted so the order can
be read against the version above:

- ~~Check whether `V9X_DD_ENGINE_VALID` is set on a family whose `EngineType`
  is `NONE`.~~ **No.** Only a chip with a `fill_engine_descriptor` hook sets
  it, and four of the six families have none. The selector tests the mode
  first; see mode 2 above.
- ~~Where to stamp the mode so DriverInit can see it.~~ **`v9x_dd_block()`.**
  It runs on the `DDGET32BITDRIVERNAME` escape, before DriverInit, and the
  active PCI device is already resolved there.

What is left:

1. ~~Mode plumbing: `[Velocity9x] Direct3D` in `SYSTEM.INI`, read at enable
   time, masked against `engine_caps` the way `gdi_accel.c` masks its own
   keys.~~ **Done**, and it needed no new shared-block field: mode 1 masks the
   capability the descriptor already carries. Mode 2 still needs the mode
   itself visible to the HAL, and `V9X_DD_ENGINE.reserved1` is where it goes.
2. ~~**Mode 1** end to end.~~ **Done and measured on `Win86SE`.**
3. ~~**On the back of mode 1**, probe what changing a Direct3D capability set
   at run time actually requires.~~ **Restart.** A re-enable moves the driver
   but not DDRAW's device list, which leaves an application enumerating a HAL
   that fails to create - the worst of the three states.
4. Settings page reports the effective mode, then writes it, with wording that
   step 3 licenses. **Reporting is done; writing is the open half.**
5. Measure CPU write cost into a video-memory surface against system memory,
   on BARRY.
6. Settle where the render target lives, and the system-to-video path if it is
   not VRAM.
7. Invert the engine selector.
8. Rasterizer core, host-tested, red then green: traversal, spans, Gouraud.
9. Depth compare and write mask, host-tested, all eight functions.
10. Texture sampling, point then bilinear.
11. `describe_caps` publishing only what 8-10 verified.
12. **Mode 2** probe ladder on a guest, per mode.
13. Final Reality on BARRY.

Steps 5 and 6 are the ones that can change the shape of mode 2, and step 3 is
the one that can change the shape of the settings page. None of them needs a
rasterizer to have been written.
