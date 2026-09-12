# Hardware Direct3D on the Intel GMA 950

## Context

Velocity9x drives the 945GSE netbook today through the chip-agnostic VBE tier-0
package: BIOS mode set, linear framebuffer, CPU drawing. That works and is
measured - a 1024x576x32 desktop, DirectDraw to `Result=COMPLETE`, 251 traced
callbacks, zero engine timeouts. What it cannot do is use the chip. Direct3D on
that machine is either absent or the CPU rasterizer on an Atom N280.

This plan takes the render engine and leaves the display to the VBIOS, with
hardware Direct3D as the destination.

The 3D seam is already the right shape and was built with this chip in mind.
`V9X_D3D_ENGINE_OPS` in [d3d_internal.h](src/display32/d3d/d3d_internal.h) is
four function pointers and a limits struct, and its header records that
`draw_triangles` is a *batch* entry precisely because "every plausible next
engine - 3dfx FIFO, ATI Rage setup, Intel ring/batch - is a command-stream
engine that needs to see a run of work to build one packet from". Adding an
engine is one new file, two selector arms, one ABI value and a manifest.

The work is not in that seam. It is everything underneath it, and the honest
headline is this: **Gen3 has no MMIO-immediate rendering path at all.** Every
command, including a plain fill, is dwords written into a ring buffer in
graphics memory and consumed after a tail-pointer write. Every card this
project has driven so far offered register-poked drawing. This one does not.
So before a triangle exists there must be a graphics-address allocator, GTT
ownership, ring submission, completion detection, cache-domain rules and a
recovery story - on a machine with no serial port and no second sample.

### Decisions taken

| Decision | Choice |
|---|---|
| Route | Straight at Direct3D. No 2D engine ships, no `V9X_ENGINE32_OPS` Intel arm. |
| Display | Engine only. Pipes, DPLL, LVDS, panel fitter and plane stay the VBIOS's. |
| Risk | Scratch Win98 install. Wedging the machine is an acceptable cost. |

Engine-only ownership also keeps the driver clear of the documented erratum
where disabling the VGA plane without screen-off first hangs the chip. That
hazard belongs to a native mode set, which this plan does not do.

### One dissent, recorded

Codex Sol (gpt-5.6-sol, high effort) independently assessed this and
recommended stopping at ring-based 2D, treating hardware 3D as an experimental
follow-on. The disagreement is about product scope, not about the phases: its
Phases 1-4 and this plan's Phases 1-4 are the same read-only-then-ring
sequence. Going straight at 3D is viable because the ring work is identical
either way. What is given up is a shippable intermediate that would have
delivered value if 3D later proves unreachable.

## Prerequisite, and it is a blocker

**The mini-VDD writes S3 registers on Intel silicon today.** `V9xMini_Set_Dpms`
in [loader.asm](src/minivdd32/loader.asm) writes `06h` to SR08, read-modify-
writes SR0D and clears CR56[2:1] with no family or chip guard, so the generic
VBE mini-VDD runs those writes on whatever silicon it lands on. On the GMA 950
those are not the registers the code believes it is writing.

Nothing Intel-specific may be armed until this is guarded. Taking ownership of
a chip's render engine while another part of the same driver scribbles foreign
registers at DPMS time makes every hang ambiguous. This is small, cheap and
independently shippable. Do it first.

**Completed 2026-09-12:** only the S3-family mini-VDD now assembles the SR0D
and CR56 body; every other family gets a one-byte no-op. The decision and build
evidence are in
[2026-09-12-intel-prerequisite-s3-dpms-guard.md](../decisions/2026-09-12-intel-prerequisite-s3-dpms-guard.md).

## Phases

Each phase has a done-criterion and an artefact readable after a reboot, since
there is no serial port. Phases 1-3 perform no writes.

### 1. Read-only MMIO fingerprint

**Implementation ready 2026-09-12:** the Intel package now reads BAR0 freshly
from PCI configuration on each diagnostic publication, asks the Intel-only
mini-VDD path to map exactly 512 KiB, reads the fixed 20-dword allowlist twice,
and writes raw values, deltas, decoded relationships, BAR provenance and a
`PASS`/`REVIEW` verdict to `C:\V9XDIAG\INTELMM.TXT`. The mini-VDD has no Intel
MMIO write instruction; tree checks enforce the positive family gate, the two
allowlist reads and the shared snapshot count. This is code-ready, not phase
completion: the physical netbook capture below remains the done-criterion.

Map BAR0 (512 KiB) through the mini-VDD and read a small allowlist twice:
`PGTBL_CTL 0x2020`, ring `TAIL/HEAD/START/CTL 0x2030-0x203C`, `HWS_PGA 0x2080`,
plus pipe timing, plane address and stride. Every offset is documentation-
derived from the Linux sources via
[the Gen3 audit](docs/decisions/2026-08-17-intel-gma-gen3-hardware-audit.md)
and is unconfirmed on this machine. Decode both pipes and find which one is
live rather than assuming.

BARs differ between DOS and Windows on this machine by 1 MiB, measured. Read
them from PCI config at run time and never cache one across a boot.

- **Done:** reads are stable, not all-zero or all-ones, and at least two
  independent relationships agree with the live 1024x576 mode - active
  dimensions against totals, and a plausible framebuffer address and stride.
- **Artefact:** `INTELMM.TXT` - raw values, decoded values, repeat-read deltas,
  BAR provenance.
- **Kill:** no stable fingerprint, or a decode that contradicts the live mode
  with no explanation. Do not go looking for better offsets by writing.

### 2. Read-only GTT inventory

Gen3 puts the GTT in its own BAR3, so this needs a second independent mapping.
Dump every PTE. Decode present and cache bits, physical page, contiguous runs,
and relate them to BSM, the visible framebuffer and the VBE-reported end of
memory. Sample GMADR only through PTEs already decoded valid - the audit
records that access beyond BIOS GTT coverage touches unbacked addresses.

- **Done:** the framebuffer and a proposed reserved range are backed by stable,
  explainable PTEs across repeated cold boots.
- **Artefact:** `INTELGTT.BIN` plus `INTELGTT.TXT` with a hash, run map and the
  proposed reservation.
- **Kill:** BAR3 unreadable, mappings changing during a quiet boot, or no
  safely reservable BIOS-backed region.

### 3. Firmware ownership and the event matrix

Still no writes. Capture phases 1 and 2 after boot, after each VBE mode switch,
after disable/enable, and after DPMS once the prerequisite guard is in.

This phase answers the question the whole plan rests on: **does the VBIOS leave
the render ring disabled and idle?** A working display proves the GTT maps the
framebuffer. It proves nothing about the ring.

- **Done:** the events that change ring, GTT, fence or PGTBL state are known,
  and a stable quiescent takeover point exists.
- **Artefact:** `INTELEVT.TXT` - per-event snapshots with field-level diffs.
- **Kill:** the ring is enabled or firmware-owned, render state changes
  asynchronously, or a VBE mode set destroys mappings undetectably. Any of
  these ends engine-only coexistence, and native ownership is a different and
  much larger project.

### 4. Ring sandbox in stolen memory

First write to Intel silicon. Requires the one-shot arm token below.

Carve the ring, a hardware status page and a scratch page from the top of the
VBE-reported usable region, inside the range phase 2 proved valid. Reduce the
published DirectDraw heap so nothing allocates over it. **No GTT writes, no
fence or tiling registers, no interrupts, no batch buffer, no VxD page
allocation.** The BIOS already mapped this memory; borrowing it removes the
entire Win98 memory-management problem from the first write.

Submit only `MI_NOOP` and `MI_FLUSH`. Poll head with both an iteration bound
and a wall-clock deadline. Then test wrap.

Use a single `XY_COLOR_BLT` into scratch as the instrument that proves GGTT
addressing and GPU-write-then-CPU-read visibility. Six dwords against roughly a
hundred for a 3D draw. **Nothing ships and no 2D capability is published** -
this is a probe, not the 2D engine the route decision excluded. Head reaching
tail proves command consumption, not data visibility, which is why the blit
exists.

- **Done:** head reaches tail repeatedly, wrap works, guard patterns around
  scratch are untouched, display sentinels unchanged, no error register set.
- **Artefact:** `INTELRNG.TXT` - pre/post registers, intended dwords, actual
  ring memory, drain time, timeout result.
- **Kill:** one unexplained hard hang, head leaving the owned range, or
  persistent error state. A hang is not licence to try more speculative writes.

### 5. First triangle

Linear and synchronous throughout: 640x480x16, offscreen target, no texture, no
Z, no blend, one triangle list, complete state re-emitted every draw, a tiny
reviewed pass-through fragment program. Do not set the D3D capability bit.
Reach it only through a private diagnostic.

A software-versus-hardware CRC need not match at first, because edge inclusion
and subpixel rules differ legitimately. Understand the hardware result, then
promote a stable capture to a reviewed golden.

- **Done:** stable across cold boots, interior pixels correct, guard regions
  untouched, capture hash stable.
- **Artefact:** `INTEL3D0.TXT` - full state and vertex dword listing, render
  target dump and hash, per-pixel assertions.
- **Kill:** avoiding hangs requires undocumented packet experimentation, or the
  state cannot be reduced to a deterministic reviewed stream.

### 6. Useful 3D, one feature per build

One opaque RGB565 texture nearest-clamp; then one texture-stage operation;
16-bit depth test without writes; depth writes and clear; alpha test;
source-alpha blend. Each gets an isolated scene, a software reference and a
capture. Stay linear. If tiling becomes unavoidable, it is a separate phase
with its own arm token.

Translate only a small enumerated subset of fixed-function texture-stage
operations. Advertise no pixel-shader capability. A general i915 shader
compiler is out of scope and is a kill criterion if it becomes necessary.

### 7. Publish

Only now add `V9X_DD_ENGINE_TYPE_INTEL_GEN3` to both selector paths and let the
16-bit side publish narrowly measured caps. Claim `8086:27AE` rev 03 exactly.
No family-wide 945 claim: Intel OEM behaviour is already measured to be a
per-BIOS fact on this project, and a Pineview sibling behaved oppositely.

## Code shape

New engine, following the ViRGE pattern:

- `src/display32/d3d/d3d_i9xx.c` - `const V9X_D3D_ENGINE_OPS v9x_d3d_engine_i9xx`,
  five positional initialisers, plus a `static const V9X_D3D_ENGINE_LIMITS`.
  Both structs are append-only; the header records a regression from violating
  that where all rendering went black while every HRESULT reported success.
- `extern` added to [d3d_internal.h](src/display32/d3d/d3d_internal.h).
- One arm each in `v9x_d3d_engine()` and `v9x_d3d_publish_engine()` in
  [d3d_core.c](src/display32/d3d/d3d_core.c). The second matters: it runs at
  DriverInit before the 16-bit side has filled the descriptor, and missing it
  once nulled every D3D table while the probe reported the opposite.
- Leaf units beside it, on the `d3d_zfixed.c` precedent, so they reach the host
  suite.
- Added to `$sources` in
  [build-ddraw-hal-dll.ps1](scripts/build-ddraw-hal-dll.ps1), to the contract
  file list in [check-tree.ps1](scripts/check-tree.ps1), and its macro prefix
  `V9X_I9XX_` to the forbidden-in-core list at check-tree.ps1:389.

Shared ABI, all appends:

- `V9X_DD_ENGINE_TYPE_INTEL_GEN3 3ul` in
  [engine_abi.h](include/velocity9x/engine_abi.h). 3 is the next free value.
- Any quirk bits as `V9X_DD_ENGINE_CAP_I9XX_*` from 0x200, the next free bit.
- **The engine descriptor needs a second base address.** The ViRGE hook derives
  its control aperture as `framebuffer_linear_base + 0x01000000`, which cannot
  express Gen3's independent BAR0 and BAR3. `V9X_DD_ENGINE` in
  [win9x_ddraw_abi.h](include/velocity9x/win9x_ddraw_abi.h) is append-only and
  carries a spare `reserved1`; add a GTT base and size there and bump the ABI
  stamp.

Family: revive the archived scaffolding from tag `archive/intel-gma-tier0`
(manifest, `intel_backend.c`, `intel_hw16.c`, `gma950_hw16.c`,
`intel_gma.h`). It is 27AE-only by design and its backend deliberately returns
`UNSUPPORTED` from `enter_mode`, which stays true under engine-only ownership.
Start at `EngineType = 'NONE'` and move it only at phase 7. Do not port the
branch's `vbe-cache.ps1`; the dynamic-VBE runtime walk superseded it.

## Host-testable leaf units

Most of this backend can be pure. Build it that way from the start, because
only arithmetic can be tested off the machine and the machine is expensive.

- **Address:** PCI/BAR snapshot decoder; GTT PTE encode/decode; PTE-run
  analyser; aperture interval allocator; memory budget; relocation with an
  overflow-and-wrap bounds checker.
- **Ring and packets:** free-space from head/tail/size; qword alignment and
  wrap planning; MI packet builders; `XY_COLOR_BLT` and `XY_SRC_COPY_BLT`
  builders with pitch/coordinate/bpp validation; a decoder that walks every
  emitted dword, checks packet lengths and refuses unknown opcodes.
- **3D state:** destination-buffer, draw-rect and scissor builders; immediate
  state assembly; blend/alpha/depth encoders; texture format, pitch and mip
  offset maths; map and sampler state; the small texture-stage compiler;
  primitive header and vertex-run construction.
- **Numeric:** bit-preserving float transport; float-to-fixed on the existing
  controlled `fistp` pattern; colour packing; Z conversion and clamp; golden
  edge cases at NaN, infinity, signed zero, the coordinate limit and maximum
  pitch.
- **Policy:** dirty-state resolver; mode-epoch state machine; poison latch and
  arm-token state machine; a log parser and snapshot differ so raw machine
  captures become host assertions.

Reviewed golden dword streams are the highest-value artefact here: they let a
packet be checked by reading before it is executed by a chip that cannot be
debugged.

## Arm and disarm

Acceleration starts absent, not idle. Capability bits stay clear until
initialisation and a private self-test both pass. A hard reset must not repeat
the experiment that caused it.

```ini
[Velocity9x]
IntelAccelDefault=0
IntelArmOnce=
IntelInFlight=
IntelEnableThisBoot=0
IntelLastResult=
```

A DOS helper runs before Windows. If `IntelInFlight` is non-empty a previous
attempt did not complete: it records `incomplete-reset:<token>`, forces
`IntelEnableThisBoot=0` and that boot is unaccelerated. Otherwise it moves
`IntelArmOnce` to `IntelInFlight` and sets `IntelEnableThisBoot=1`. The driver
arms only when the token, PCI id, revision, phase and packet CRC all match.
Immediately before the first risky write the 16-bit side persists
`IntelEnableThisBoot=0` and re-reads it, so the on-disk state is already
disarmed while the session stays armed. Clean shutdown records `pass:<token>`
and clears `IntelInFlight`.

Persist an intent record - token, phase, command CRC, owned ranges - before
each risky command. The absence of its completion record is the evidence after
a reboot that there is no serial port to give.

Every wait carries both an iteration bound and a wall-clock deadline. On
timeout: latch poison, drop capabilities for the session, reject further
submission, do not retry, do not free pages the ring may still reference, fall
back to the CPU path. There is no measured reset path on this chip; ring
re-init is not a GPU reset. Safe Mode ignores every arm key.

## Kill criteria

- **Firmware owns the ring.** Phase 3 finds it enabled, non-idle or changing
  asynchronously. Engine-only coexistence is then unavailable.
- **945GSE errata.** Intel's mobile 945 specification update documents a
  battery-mode 3D hang tied to Dual-Frequency Graphics Technology, and an
  internal-buffer flush erratum affecting 945GSE that can hang the system,
  stating only that a workaround exists in later Intel Windows drivers. That
  workaround is not public. If it cannot be reconstructed from
  licence-compatible sources, or AC success turns into unexplained battery
  failure, stop. Audit this before phase 4, not after.
- **Identity.** The chip is 27AE; the ROM's PCIR says 27A2. Select from the
  measured PCI identity, never the ROM string. If phases 1-3 do not match the
  documented model, stop.
- **Second function.** `27A6` class 0380 exists. Bind function 0 only; never
  reset or reprogram function 1. Stop if it owns resources that cannot be
  separated.
- **Performance.** Stop and keep the software engine if reliability
  permanently requires draining after every triangle, or hardware does not
  materially beat `d3d_soft`, or useful workloads cannot fit stolen memory and
  dynamic GTT mapping fails, or depth and texturing need tiling that cannot be
  safely owned, or coverage starts to approach a general shader compiler.
- **Single sample.** A pass supports this 27AE rev 03 machine and nothing
  else. Stop after repeated forced resets or any inability to separate code
  defects from sample-specific behaviour.

## Verification

- `./scripts/check-tree.ps1` after every structural change, and
  `./scripts/build-host.ps1` after every leaf unit. Both are compiler-cheap and
  neither needs the netbook.
- `./scripts/run-checks.ps1` green before any commit that changes code.
- `./scripts/run-family-enable-gate.ps1` when the shared 16-bit layer changes,
  because a mistake there is a four-family mistake.
- `run-vm-mode-matrix.ps1` cannot help: the family declares
  `Vm.Emulator = 'none'` and nothing emulates any Gen3 part. The manual gate is
  [the bring-up runbook](docs/specifications/intel-gma-bringup-runbook.md),
  which exists for exactly this.
- Per phase: the named artefact off the machine, plus a dated decision record
  under `docs/decisions/` carrying the capture and the hypotheses the evidence
  killed. That record is the deliverable of a phase, not a side effect of it.

## Not in scope

Native mode set, and with it any mode above 576 lines through the panel
fitter. A shipped 2D engine. Tiled surfaces. Batch buffers. VxD page allocation
and dynamic GTT ownership, which stolen memory avoids entirely for now. Any
claim about Gen3 parts other than this one.

## Loose end from the previous task

The VBE software-Direct3D default change is complete and gated but uncommitted,
and an untracked `shot_now.png` from a QEMU screendump is sitting in the repo
root. Both want clearing before Intel work starts on a branch.
