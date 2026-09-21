# ViRGE 325 on VESA Local Bus, through old MMIO

Date: 2026-08-22, substantially revised 2026-09-22
Status: revised plan; implementation has not started. **The acceleration
premise is now known to be unsound as written** - see
[the mixing decision](../decisions/2026-09-22-virge-vl-mmio-framebuffer-mixing.md)
and "What the defect actually is" below. Stages 0-2 remain worth doing as PCI
work. Stage 4 survives only as a way to measure the narrow
full-screen-exclusive hypothesis, and the B-window and any ViRGE/DX
substitution are rejected outright.

## Outcome

Make Velocity9x's existing ViRGE DirectDraw fill and screen-to-screen BitBLT
path reachable on a VESA Local Bus ViRGE 325. Keep PCI ViRGE/DX on new MMIO,
select old MMIO only when the S3 chip was identified through the no-PCI register
path, and do not advertise Direct3D on that path.

There are two hard gates before a VLB emulator or physical board is useful:

1. Velocity9x must recognise the **86C325 (`5333:5631`)** used by the MK-765VL
   experiment. It currently recognises only the ViRGE/DX 86C375
   (`5333:8A01`).
2. Physical `0xA0000` must be turned into a valid flat linear address for
   `V9XHAL.DLL`. Writing the physical address directly into
   `control_linear_base` is not a mapping and must not be treated as one.

The plan stops at either gate if it fails. It does not use a VLB emulator to
paper over an unsupported chip or an unproved Windows mapping mechanism.

## Why old MMIO is the right interface

The current driver enables new MMIO with CR53[3] and publishes the control
window as the framebuffer's mapped linear base plus 16 MiB
([virge_hw16.c:26-53](../../src/chipsets/s3/virge/virge_hw16.c)). Every S3D
access then adds an unchanged register offset to that flat base
([eng_s3_virge.c:35-46](../../src/display32/engines/eng_s3_virge.c)).

That is a PCI layout. In VL mode the ViRGE receives A2 through A22 and external
SAUP1/SAUP2 decoding selects its primary and secondary 8 MiB spaces. It cannot
distinguish the LFB from a control window 16 MiB above it. The detailed account
is in [mkarcher's reply 206](https://www.vogons.org/viewtopic.php?start=200&t=76647),
and reply 208 notes that a new-MMIO driver can usually be ported by changing
the enable and base-address setup — while warning in the same post that the
port is where the ViRGE's broken VL bus interface issues begin (see the
secondhand-report section below).

The primary source is stronger than either that thread or an emulator: section
15.1 of the
[S3 ViRGE databook](https://www.dosdays.co.uk/media/s3/ViRGE/S3_ViRGE_325_Register_Documentation.PDF)
states that new MMIO is PCI-only and defines the old-MMIO forms exactly:

| CR53[4:3] | Mode |
| --- | --- |
| `00b` | MMIO disabled; the VL power-on state |
| `01b` | new MMIO only |
| `10b` | old MMIO only |
| `11b` | both |

With old MMIO selected, CR53[5] chooses one of these layouts:

| CR53[5] | Physical decode | Software base that preserves current offsets |
| --- | --- | --- |
| `0` | `0xA0000-0xAFFFF` | mapped linear address of `0xA0000` |
| `1` | `0xB8000-0xBFFFF` | mapped linear address of `0xB8000` **minus `0x8000`** |

The bias in the second row is load-bearing. `SUBSYS_STAT` remains register
offset `0x8504`; in the B-window form it must land at physical `0xB8504`, not
at `0xC0504`. All register offsets the current 2D backend uses are between
`0x8504` and `0xA50C`, so both old-MMIO forms can serve it without changing
the engine code. The A-window's lower 32 KiB is the image-transfer area; the
B-window omits that area, which Velocity9x does not use.

The B-window form is documented here for completeness only. It is rejected on
VL for a reason the databook does not cover; see "What the defect actually is"
below.

The vendored 86Box model agrees with the databook: CR53[4] enables its old-MMIO
mapping, CR53[5] chooses `0xA0000`/64 KiB or `0xB8000`/32 KiB, and both mappings
feed the same handler
([vid_s3_virge.c:1290-1305](../../build/reference/86box/src/video/vid_s3_virge.c)).
That agreement makes 86Box a useful test oracle, not the source of the hardware
contract.

## Secondhand report of mkarcher's own attempt

Received 2026-08-22, relayed by a correspondent who knows mkarcher — the same
person whose Vogons replies 155, 206 and 208 this plan is built on:

- mkarcher tried to work around the MMIO issue for his own VLB ViRGE 325 and
  failed, including an attempt with a CPLD — a stronger hardware workaround
  than the single-flip-flop SAUP2 delay this plan's Stage 4 specifies.
- He concluded S3 had good reason not to ship a VLB ViRGE; every ViRGE after
  the 325 dropped the VL interface entirely.
- The 325 "works flawless as framebuffer device in VLB mode"; the MMIO issue
  is specifically what stopped his accelerated Windows driver attempts.
- He did get Terminal Velocity's S3D acceleration working on the VLB 325 by
  avoiding MMIO in that special case.

A follow-up on 2026-08-23 said the corruption especially shows on switching a
Win9x DOS window between text and graphics mode and back, but not only there,
that no reliable reproduction pattern was found, and that nothing further could
be told. This plan was revised around that report. **Both readings are now
superseded**; the section below records what replaced them.

## What the defect actually is

On 2026-09-21 and 2026-09-22 mkarcher answered this project's question directly
in the thread. The finding, the quotes and the hypotheses it kills are recorded
in
[the mixing decision](../decisions/2026-09-22-virge-vl-mmio-framebuffer-mixing.md).
In short: the MMIO window is not the variable, and the trigger is **mixing
framebuffer memory access with certain regions of MMIO space**. The exact
pattern is under NDA and will not be disclosed. The damage is silent
configuration corruption whose worst case enables bus-interface features such
as PCI retries while in VL mode, locking the bus some time later.

What that does to this plan:

- The 2026-08-23 mode-transition reading is retired. That clustering was where
  framebuffer and accelerator traffic interleave most densely, not a cause,
  and sequencing CR53 around the transition is not a mitigation.
- **The configuration this plan chose - variant A plus the high LFB - is in
  the susceptible set.** Variant A is immune only with no LFB at all, because
  then nothing can reach the framebuffer; the immunity comes from having no
  drawing path, not from the window.
- **Variant B is rejected, not a fallback.** It places a framebuffer window at
  `A000` alongside MMIO at `B800`, which is the mixing condition by
  construction, and is susceptible with no high LFB at all.
- The defect is in the VL interface alone. A PCI-connected ViRGE handles the
  same patterns perfectly, so Stages 0-2 are unaffected as PCI work.
- A ViRGE/DX cannot substitute for a 325: strap-modded to select VL it stayed
  in PCI mode, with one bus-type strap never read back at all.
- Terminal Velocity is explained rather than mysterious. A full-screen S3D
  game draws everything through the engine, so there is no unaccelerated GDI
  framebuffer traffic to interleave. The old open question about its transport
  is closed; what mattered was the absence of CPU framebuffer access, not the
  presence of a clever one.
- Most seriously, the mixing he describes - accelerator MMIO interleaved with
  unaccelerated framebuffer drawing where acceleration makes no sense - **is
  this driver's architecture**. Velocity9x is a DIB Engine driver: GDI
  rasterises into the framebuffer through `V9xScreenSelector` while the HAL
  accelerates DirectDraw through MMIO. On VL that is the steady state, not an
  edge case, from the first mouse move over an accelerated surface.

### Can all 2D go through the S3D engine instead?

The obvious response is to remove CPU framebuffer access entirely and drive
every operation through the engine, reaching the immune configuration by
construction. It is the right instinct - it targets the named defect rather
than a symptom - but as a whole-driver strategy it does not survive contact:

- **It is the full DDI this project deliberately does not implement.** Every
  operation must be hardware or it falls back to the DIB Engine, which needs a
  framebuffer pointer. There is no partial credit: one interleaved framebuffer
  write is enough to corrupt the configuration silently.
- **Unaccelerated pixels still have to cross the bus.** Operations the engine
  cannot do natively would be rendered into a system-memory shadow and pushed
  up through the image-transfer window - MMIO writes rather than framebuffer
  writes, so safe, but the same volume of VL traffic plus a second write into
  the shadow. For typical GDI work it would be slower than the DIB Engine
  driver it replaced.
- **A shadow diverges.** Once some operations run on the engine and others in
  the shadow, any operation reading existing screen content is wrong unless
  every hardware operation is mirrored in the shadow as well, which spends the
  acceleration it was meant to protect.
- **Screen-to-host is not established.** `GetDIBits` and any read-modify-write
  of screen content need pixels back. If the engine has no screen-to-host
  transfer, the only route is a CPU framebuffer read - the forbidden access.
- **DirectDraw `Lock` defeats it outright.** A locked surface hands the
  application a raw pointer and it writes video memory with the CPU. That
  cannot be intercepted, and DirectDraw is this project's entire purpose.

So: no, not as a general driver architecture. There is a narrower version worth
recording, because it is the Terminal Velocity configuration rather than a
guess at one - **acceleration only in full-screen exclusive DirectDraw**. The
desktop stays framebuffer-only and unaccelerated, which mkarcher confirms works
flawlessly; MMIO is enabled only while an exclusive full-screen DirectDraw app
owns the display, where GDI is not drawing; and a `Lock` request on a video
memory surface drops that app to unaccelerated for the rest of its session
rather than being served. That is bounded, is detectable at runtime, and
matches the one configuration known to have worked on real VL hardware. It is
still a hypothesis, it still needs the readback diagnostic below to be trusted,
and it is worth nothing until Stage 0 exists.

Whether Ironfield locks its surfaces or blits them decides whether the narrow
version would help it at all. That is measurable on PCI today, before any VL
work.

Do not buy or build Stage 4 hardware before both questions are answered,
directly or via the Vogons thread.

## What is true now

| Piece | State | Consequence |
| --- | --- | --- |
| VLB install | **Working.** The manual model installs without a mini-VDD, the devnode reaches Problem 0 / `DN_STARTED`, and the Trio64 reaches `enable-ok` | Code 24 is no longer a stage or blocker; see the [handover](../handoffs/2026-08-22-vlb-manual-select-handover.md) |
| VLB LFB | Proven at `0x7F000000` and `0x04000000` on the physical 486 | Keep trusting CR59/CR5A; see the [aperture decision](../decisions/2026-08-21-vlb-aperture-answered.md) |
| ViRGE device support | ViRGE/DX 375 only | A ViRGE 325 would be rejected by `v9x_s3_identify_without_pci` before MMIO setup |
| ViRGE acceleration | DirectDraw hardware solid fill and screen copy; Direct3D on PCI | This is not accelerated GDI, and the VLB goal must not be described as such |
| Old MMIO | Not implemented | Only the CR53[3] new-MMIO signature exists in the tree |
| Framebuffer mapping | One DPMI 0800h mapping and one persistent screen selector | Old MMIO needs a second flat mapping; it is not inside the high LFB mapping |

The old plan's mini-VDD casualty is not a present blocker. The manual VLB model
deliberately installs no mini-VDD, and the S3 mini-VDD builds its VBE collection
out. The transition that matters is full-screen DOS / `ResetHiResMode`: a VBE
mode set happens first and the chip hook must then re-establish old MMIO, just
as it re-establishes new MMIO today.

## Design decisions

### Select by the route that identified the card

Do not probe new MMIO and fall back on VLB. A read from an undecoded high range
does not reliably identify a bus, and the accepted hardware path already knows
the answer:

- a successful `V9xHardwarePresent` result is a PCI configuration-space match;
- a successful `identify_without_pci` result occurred only after the PCI BIOS
  was absent and is the register-identified VLB/ISA route.

Record that distinction as an explicit access-path enum or getter when
`v9x_hardware_acceptable` accepts the card. Do not infer it later from
`v9x_pci_match`: the no-PCI identifier deliberately writes the same device
index as the PCI scan.

This deliberately supports the present 486, which has no PCI BIOS. It does not
make a VLB ViRGE safe to identify in a mixed PCI/VLB machine: the current safety
rule refuses extended-register probing whenever a PCI BIOS exists but no PCI
device matched. Supporting that topology needs an authoritative way to identify
the non-PCI adapter and is outside this plan.

The ViRGE object may contain a small dispatcher, but the PCI-new and
register-old implementations remain separate leaf functions. The per-object
audit can then require both exact CR53 masks while still proving neither
sequence appears in `trio_hw16.obj`.

### Map control memory; do not publish a physical literal

Add a second persistent physical mapping for an independently located control
window. For the old-MMIO path it maps physical `0xA0000` for 64 KiB and stores
the DPMI-returned flat address. The mapping is established before CR53 exposes
old MMIO, so a mapping failure leaves the legacy VGA decode unchanged.

The mapping has no DIB Engine selector and must not disturb
`V9xScreenSelector`. The 32-bit HAL needs only the flat address. Keep the
mapping for the driver's lifetime, matching the framebuffer mapping's existing
stability rule across Disable/Enable cycles.

Extend `fill_engine_descriptor` to receive the independently mapped control
linear address explicitly. On PCI it continues to derive LFB-linear + 16 MiB;
on the register path it uses the returned low-memory mapping. No chipset hook
may write `0xA0000` directly into `control_linear_base`.

Append new failure stages for the control mapping; do not renumber the existing
published stage codes. Diagnostics must state the access path, MMIO mode,
physical control window, and mapping status.

### Program one MMIO mode, exactly

For PCI, write CR53[4:3] as `01b`. For VLB, write it as `10b`. Preserve
unrelated CR53 bits, choose CR53[5] deliberately, and read back the complete
three-bit field. Merely OR'ing bit 4 would leave the PCI power-on bit 3 set and
test both modes at once.

Do not use `ADVFUNC_CNTL` bit 5 as an automatic fallback. CR53 is documented,
already covered by the extended-register unlock path, and can be verified by
readback. A second enable mechanism would add state without solving the flat
mapping gate.

### On the register path, do not leave old MMIO enabled at rest

Keep old MMIO exposed only while the driver is actually issuing S3D commands:
set CR53[4:3] to `10b` before a batch of accelerated operations, restore `00b`
once the batch has drained and status is idle.

The justification is not the DOS-box transition, which is retired as a cause.
It is that the defect needs framebuffer and MMIO access to be mixed, and a
closed window makes the accelerator unreachable, so there is nothing to
interleave with GDI's framebuffer writes. That also sets the real requirement,
which is stricter than the CR53 writes themselves: **no framebuffer access may
occur between opening and closing the window.** With the DIB Engine free to
rasterise on another thread's GDI call, bracketing CR53 alone does not deliver
that. Whether Win9x's serialisation actually holds GDI off for the duration of
a DirectDraw HAL call is checkable, and it must be checked rather than assumed
before this is called a mitigation.

Treat it as a **hypothesis to measure on the register path only**, not as a
fix. A mitigation that narrows a window cannot be shown correct by passing
tests when the failure it prevents is silent and delayed. PCI keeps its
current always-enabled new-MMIO behaviour; do not introduce batch bracketing
there, where it would add state and risk to a working path.

If bracketing is implemented, the enable and disable must be the same exact
CR53 leaf functions the binary audit already covers, and the diagnostics must
report whether the window is currently open.

### Keep the engine backend unchanged and narrow capability

`eng_s3_virge.c` remains unchanged. If old MMIO requires different S3D
register offsets or command ordering, stop and record that the premise failed.

The register-identified old-MMIO descriptor publishes solid fill, screen copy,
flip and vblank, but clears `V9X_DD_ENGINE_CAP_D3D`. This makes "Direct3D is out
of scope" true in the exported HAL rather than merely true in prose. PCI keeps
its present capability set and D3D regression.

The manifest's per-chip caps continue to describe the PCI baseline. Add a
separate access-path test for the runtime VLB cap reduction, and make
`V9XHW.INI` derive its Direct3D statement from the effective descriptor rather
than the device entry's current static `hardware-s3d` string.

Use `0xA0000`/64 KiB. **`0xB8000` is rejected, not a fallback**: that variant
exposes a framebuffer window at `A000` alongside MMIO at `B800`, which is the
mixing condition by construction and is susceptible even with no high LFB. Do
not implement the biased base, and do not spend a test on it.

## Stage 0 - support the chip that the board uses

The published MK-765VL experiment uses a pin-compatible ViRGE 325, whose chip
ID is `5631`. The current `v9x_virge_device` is a ViRGE/DX 375 and publishes
`8A01`. Old MMIO is common to them, but identity, BIOS modes and validation
must not be conflated.

1. Inventory the 86Box PCI ViRGE 325 BIOS with the existing VBE inventory tool.
2. Add a conservative `5333:5631` device entry, manifest chip, backend probe
   identity and host tests. Reuse the S3D engine type only after checking every
   register used by `eng_s3_virge.c` against the 325 databook.
3. Give the 325 its own declared mode list from the inventory. Do not copy the
   DX list by assumption; the manual model's mode intersection will change when
   a third S3 chip is added.
4. Add a PCI 325 86Box target and prove the ordinary new-MMIO mode matrix,
   DirectDraw fill/blit, status validation, mode switching and DOS-box return.
5. Factor the ID-to-device-index match so a host test confirms CR2D/CR2E
   `56/31` selects the same entry that the PCI scan selects for `5631`; keep
   the actual port-I/O sequence covered by the 16-bit build and VM test.

**Exit:** one S3 package supports both ViRGE 325 and ViRGE/DX on PCI, the 325
mode/capability claims are measured, and no VLB-specific code is involved.

## Stage 1 - prove the low-memory flat mapping on PCI

This is a disposable or diagnostic-only spike against the now-working PCI 325.
It answers the highest-risk Windows question before the shared ABI or hardware
table is expanded.

1. Attempt DPMI 0800h for physical `0xA0000`, length `0x10000`, while retaining
   the normal high LFB mapping. Record carry, returned flat address and failure
   stage. Do not assume low physical memory is identity mapped.
2. Read a stable idle `SUBSYS_STAT` through new MMIO, program CR53 old-only,
   and read the same register repeatedly through the mapped A-window. Restore
   new-only before leaving the probe.
3. At 8 or 16 bpp, run one guarded fill and one guarded screen copy through the
   old base, then verify the pixels through the LFB. Do not run D3D; the current
   2D backend deliberately declines depths above 16 bpp.
4. Exercise VBE re-entry / `ResetHiResMode` and show that the mapping remains
   valid while CR53 is re-established after the BIOS call.

If DPMI 0800h refuses the low range, stop. The next plan must choose and prove a
Windows-specific mapping service, likely through a loadable mini-VDD/VxD or a
documented VMM service. An identity pointer is not the fallback, and the
current Win95 manual model cannot silently acquire a mini-VDD that is known not
to load there.

**Exit:** a recorded, repeatable flat mapping of the old-MMIO window and a PCI
325 executing the existing 2D commands through it.

## Stage 2 - integrate the deterministic old-MMIO transport

1. Add the explicit PCI-vs-register access-path state at the point hardware is
   accepted, with host tests for both routes and for a refused foreign card.
2. Add the independent control-window descriptor and DPMI mapping helper. Map
   it before the existing chip enable, retain it across Disable/Enable, and
   leave the framebuffer selector/reuse rules unchanged.
3. Split the ViRGE aperture leaf functions:
   - PCI: shared S3 LFB enable, CR53 new-only, existing descriptor and full caps;
   - register path: shared S3 LFB enable, CR53 old-only at A0000, mapped control
     descriptor and no D3D cap.
4. On every reset path, run VBE mode entry first and then reapply the selected
   CR53 mode. Validate status again before the next accelerated operation, as
   the HAL already does after Enable/ReEnable.
5. Publish `AccessPath`, `MmioMode`, `ControlPhysicalBase`,
   `ControlMappingStatus` and the effective Direct3D state in the S3
   diagnostics.
6. Extend the binary audit with anchored exact-mask signatures. Keep the
   sibling rule: neither old nor new ViRGE CR53 code may appear in the Trio
   object.

Run the complete existing PCI DX and Trio64 matrices. The DX must remain on new
MMIO with D3D; Trio64 output and signatures must be unchanged. Re-run the PCI
325 old-MMIO forced test from Stage 1 through the integrated path.

**Exit:** one package deterministically selects new MMIO for PCI ViRGE and old
MMIO for register-identified ViRGE, with no runtime probe and no engine-source
change.

## Stage 3 - model a ViRGE 325 VLB board in 86Box

This is more than changing `DEVICE_PCI` to `DEVICE_VLB`. The current ViRGE
model always registers PCI configuration space, gates all mappings on PCI
command-memory-enable, raises PCI interrupts, starts CR53 in the PCI new-MMIO
state, and disables the legacy ROM mapping until PCI enables it.

Add a bus kind to `virge_t` and a 325 VLB device with these behaviours:

- no `pci_add_card`, PCI config callbacks or PCI IRQ calls;
- the VGA I/O and ROM decode enabled as legacy resources;
- 2 MiB VRAM, VLB timing, and CR2D/CR2E = `56/31`;
- a fixed external secondary decode for the LFB, with CR59/CR5A initialised to
  the same address and writes unable to move the board decoder;
- a primary low decode for VGA and old MMIO;
- new-MMIO mapping disabled in VLB mode regardless of CR53[3], so LFB + 16 MiB
  reads as undecoded while the same model's PCI form responds there;
- old MMIO implemented by the existing common handler at A0000/B8000.

Use a VLB machine with no PCI BIOS so the driver's register access path is
actually exercised. The test ROM must be a ViRGE 325 ROM suitable for VL mode;
do not call a PCI ROM representative without documenting why its feature-pin
initialisation is irrelevant to the emulator.

This model deliberately does **not** simulate the ViRGE's broken VL transaction
patterns, SAUP timing corruption or accidental DMA activation. It proves
address selection, identification and driver sequencing only.

**Exit:** the integrated driver reaches `enable-ok`, reports old MMIO and no
D3D, passes guarded DirectDraw fill/blit, survives DOS-box return, and reads
`0xFFFFFFFF` at the new-MMIO candidate on the same emulated board.

## Stage 4 - physical ViRGE 325, only after Stages 0-3

This stage is **expected to fail as written**: the secondhand report above
says the author of the SAUP2 workaround could not make an accelerated Windows
driver work on his own VLB 325, even with a CPLD. The mixing decision makes
that sharper: the configuration this plan chose is in the susceptible set, and
the only immune one cannot draw. Stage 4 stays in the plan only as the way to
measure a specific bounded hypothesis - the full-screen-exclusive design
above - and because a negative result is bounded, rejecting the physical
acceleration claim and nothing else.

Do not start it to test the plan as originally written. The question it can
still answer is whether a configuration with no CPU framebuffer access while
MMIO is live survives on real VL hardware.

Required hardware and firmware:

- an MK-765VL-class board and a ViRGE 325. **A ViRGE/DX cannot stand in**:
  strap-modded for VL it stays in PCI mode, and one bus-type strap is never
  read back at all;
- a VL-correct ViRGE ROM whose mode list was inventoried in Stage 0;
- 1 WS late decode and mkarcher's one-VL-clock SAUP2 delay, using 74ACT74 or
  74F74 as described in
  [reply 155](https://www.vogons.org/viewtopic.php?start=140&t=76647);
- a repeatable hard-reset recovery path, because the known failure mode is a
  locked local bus.

Bring-up order is deliberately narrow:

1. DOS POST and unaccelerated VBE/LFB baseline.
2. Windows desktop through the LFB with the old-MMIO capability disabled.
3. Enable old MMIO and perform read-only status sampling.
4. One small off-screen solid fill, verify through the LFB, then one small
   screen copy.
5. The DirectDraw regression and DOS-box return; no Direct3D and no command DMA.

**Every step above is instrumented by configuration readback, not by waiting
for a hang.** The corruption is silent and the lockup arrives later, so a step
that completes proves nothing on its own. After each step, read the
bus-interface configuration registers back and compare them against what was
written; a divergence is the defect, caught where it happened. Record the
readback with every result, including the passes.

Order the steps so the mixing itself is the variable. Step 3 with no
framebuffer access at all is the immune configuration and must come back
clean - if it does not, the fault is elsewhere and the rest is meaningless.
Step 4 issues engine work with the desktop idle. Only then introduce
deliberate mixing: GDI drawing interleaved with accelerated operations, which
is what the shipped DIB Engine driver does continuously. If readback stays
clean without mixing and diverges with it, that confirms the decision doc on
this project's own hardware and is the most useful finding this stage can
produce.

Use the driver's no-clear VBE mode flag so its own mode entry does not ask the
BIOS to clear the framebuffer through an engine path. A ROM may still use the
engine during POST or other mode transitions, so the ROM behaviour is part of
the hardware gate, not something the Windows driver can assume away.

The thread records two distinct hazards that the emulator cannot validate:

- valid VL access patterns can corrupt control registers or accidentally start
  unsupported bus-master DMA and lock the bus (reply 155);
- the interface has driver-workaround sequences beyond merely selecting old
  MMIO (replies 203, 205, 206 and 208).

Do not invent those sequences. The secondhand report above says no working
patched-driver sequence is known to exist — mkarcher himself failed with
hardware workarounds up to a CPLD. If the delayed-SAUP2 board fails, that is
the expected outcome; record the exact failure signature (which access, which
register window, read or write) rather than iterating on workarounds. A hang
here rejects the physical acceleration claim; it does not invalidate the PCI
old-MMIO transport or emulator work.

**Exit:** guarded physical fills and copies complete repeatedly, pixel results
match, mode/DOS transitions recover, and no bus lock or DMA activation occurs.

## Validation matrix

| Target | MMIO | Required result |
| --- | --- | --- |
| PCI ViRGE/DX 375 | new only | Existing full mode matrix, DirectDraw and D3D unchanged |
| PCI ViRGE 325 | new only | New chip-support baseline |
| PCI ViRGE 325, forced test | old A-window | Status, fill, copy and reset proof; no D3D |
| PCI Trio64 | none / port engine | Existing matrix and object audit unchanged |
| Physical Trio64 VLB | existing port engine | Manual install, `enable-ok`, modes and reset regression unchanged |
| Emulated ViRGE 325 VLB | old A-window | New MMIO absent; fill/copy and reset pass; no D3D |
| Physical ViRGE 325 VLB | old A-window | Guarded hardware exit criteria above |

## Expected file-level changes

Stage 0:

- `include/velocity9x/s3_virge.h` and `src/chipsets/s3/virge/backend.c` - 5631
  identity and host backend support
- `src/chipsets/s3/virge/virge_hw16.c` and `src/chipsets/s3/s3_hw16.c` - 325
  device entry
- `packaging/families/s3/family.psd1` - chip, modes, symbols and PCI VM target
- host identity/backend/family tests and a 325 VBE inventory decision

Stages 1-2:

- `src/display16/runtime.asm` - independent flat physical mapping primitive and
  accessor; no DIB selector
- `src/display16/enable16.c` and `src/display16/dd16.c` - access-path state,
  mapping order, appended failure stages and descriptor plumbing
- `include/velocity9x/hw16.h` and `include/velocity9x/win9x_ddraw_abi.h` -
  control-window contract and clarified addressable-offset semantics
- `src/chipsets/s3/virge/virge_hw16.c` - exact old/new CR53 leaves and
  capability split
- S3 diagnostics, manifest audit, `CHANGELOG.md`, tests and a stage decision

Stage 3 changes only the vendored/local 86Box test platform and its profile; it
is not part of the shipped driver.

Not modified in Stages 0-3:
`src/display32/engines/eng_s3_virge.c` and the ViRGE command/register constants.

## Open questions, in decision order

**Answered and closed, 2026-09-22**, by
[the mixing decision](../decisions/2026-09-22-virge-vl-mmio-framebuffer-mixing.md):
which access patterns trigger the corruption (mixing framebuffer and MMIO
access; exact pattern under NDA), what Terminal Velocity did differently (it
had no unaccelerated framebuffer drawing to interleave), and whether the
B-window or a ViRGE/DX offers a way round (neither does). None of these can be
refined by asking further.

1. Does Ironfield - or the DirectDraw applications this driver targets - `Lock`
   video memory surfaces, or does it blit into them? Measurable on PCI today,
   and it decides whether the full-screen-exclusive design would help any real
   application. Cheapest open question here and the only one needing no new
   hardware.
2. Does the Windows DPMI host map physical `0xA0000` with function 0800h while
   the high framebuffer mapping remains live?
3. Does Win9x serialisation actually hold the DIB Engine off for the duration
   of a DirectDraw HAL call, so that a bracketed MMIO window can guarantee no
   framebuffer access inside it?
4. Which measured VBE modes and memory configuration are honest for the 2 MiB
   ViRGE 325 ROM used by the emulator and eventual board?
5. Which VL-correct ViRGE ROM is available for redistribution or local testing?

Question 1 gates whether the narrow acceleration design is worth building at
all. Question 2 blocks all integrated old-MMIO work. Question 3 blocks calling
bracketing a mitigation. Question 4 blocks claiming support for the actual
physical chip. Question 5 does not block Stages 0-2.
