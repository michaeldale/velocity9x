# Velocity9x Ground-Up Development Plan

> This is the original planning baseline and is kept as a historical record.
> Parts of it are out of date — Direct3D is listed here as deferred scope but
> has since been implemented on the ViRGE. For what the driver does today, see
> [README.md](README.md) and [CHANGELOG.md](CHANGELOG.md).

Status: planning baseline, revision 2 (2026-08-08)  
Initial target: Windows 98 Second Edition on S3 ViRGE/DX 86C375  
Initial graphics scope: Windows GDI and DirectDraw  
Deferred scope: Direct3D, S3D, OpenGL, video overlay, and additional chipsets

## 1. Project objective

Velocity9x will be a new Windows 9x display-driver family written from the ground up. The first release will support the S3 ViRGE/DX 86C375, initially under emulation and later on physical hardware. The design must allow later S3 Trio, Matrox, and 3dfx backends without tying the common driver code to one chipset.

The project will own the copyright in its original implementation. The intended destination is release under an OSI-approved open-source license, to be selected and reviewed before the first public release (decision recorded 2026-08-08). Until a license is deliberately selected, the code is **all rights reserved** and no permission to copy, modify, or redistribute it is granted.

The practical target for the first useful release is:

- Reliable Windows 98SE installation, boot, shutdown, restart, and uninstall.
- A linear framebuffer with stable 8- and 16-bit display modes, plus 24- or 32-bit modes as verified hardware support permits.
- Correct software-rendered GDI through the Windows DIB Engine.
- Hardware cursor, solid fills, and BitBlt acceleration.
- A conservative DirectDraw HAL with video-memory surfaces, locking, blitting, color keys, page flipping, and vertical-blank synchronization.
- Diagnostic logging, recovery controls, and accurate capability reporting.

## 2. Supported systems and initial boundaries

### 2.1 First supported configuration

- Guest OS: Windows 98 Second Edition.
- CPU model: Pentium II-class, one virtual CPU.
- RAM: 128 MB initially.
- GPU: S3 ViRGE/DX 86C375, 4 MB VRAM where available.
- Bus: PCI.
- Display baseline: 640x480 at 8 bpp and 60 Hz.
- Primary emulator: 86Box.
- Debug transport: COM1 redirected to a host file or named pipe.

Windows 95 and Windows Me support are compatibility goals after the Win98SE DirectDraw milestone. Supporting all three during initial bring-up would multiply installer, DIB Engine, DirectX runtime, and screen-switching variables too early.

### 2.2 Explicitly deferred

- Direct3D and S3D.
- OpenGL ICD or wrapper.
- Video overlay and capture.
- AGP-specific features.
- Multiple monitors or multiple adapters.
- System suspend/resume and adapter power-down. Monitor low-power states are
  intentionally not advertised until legacy VESA resume is framebuffer-safe.
- Native EDID timing generation until fixed modes are stable.
- A configuration GUI or control panel; the initial releases expose per-primitive registry switches only.
- S3 Trio, other ViRGE variants, Matrox, and 3dfx backends.

#### Parked follow-up: monitor sleep

Monitor sleep is deferred while the driver advertises D0 only. Start with a
1-3 day validation spike that re-enables D3 in a diagnostic build, exercises
the real Win98 idle timer and keyboard/mouse wake path, and runs at least 20
sleep/wake cycles across every supported resolution and color depth. Synthetic
`SC_MONITORPOWER` messages are not an acceptance test because they can bypass
the power-manager capability path.

If the managed idle path reaches the existing mini-VDD VESA/power callbacks
and preserves the active framebuffer, promote it after the cycle matrix passes.
If Win98 bypasses those callbacks or loses mode/framebuffer state, budget a
larger 1-2 week task to implement and verify transition-state preservation
before advertising monitor low-power capabilities.

## 3. Driver architecture

The design should minimize 16-bit code and keep portable logic testable on the host.

```text
Windows GDI / DirectDraw runtime
             |
      16-bit display DRV
       - DIB Engine glue
       - required entry points
       - thin thunks only
             |
       32-bit mini-VDD
       - PCI and resources
       - mode and screen state
       - MMIO/LFB mapping
       - engine synchronization
       - diagnostics and recovery
             |
        S3 hardware layer
       - probe and capabilities
       - clocks and timings
       - framebuffer and cursor
       - blit/fill primitives
             |
       DirectDraw HAL module
       - capabilities
       - surfaces and heaps
       - blit/flip/lock/status
```

This split is a working hypothesis, not a settled design. In shipped Windows 9x drivers, much of the mode-setting and hardware programming historically lived in the 16-bit display driver via the DIB Engine, and the mini-VDD's documented role centers on DOS-box screen switching and state save/restore. Pushing work into the 32-bit VXD is desirable but unproven; the toolchain spike must establish how much logic the 16-bit side is actually required to hold.

Exact binary boundaries should be proven during the build spike. Expected outputs are:

- A 16-bit DIB Engine display `.DRV`.
- A 32-bit mini-VDD `.VXD`.
- A 32-bit DirectDraw HAL `.DLL` if required by the selected DDI arrangement.
- A Chicago-format installation `.INF`.
- A DOS/Win32 diagnostic executable.

### 3.1 Common core

Keep these modules independent of S3 where Windows 9x ABI constraints permit:

- Fixed-width types and segmented-pointer helpers.
- Logging and assertions.
- Mode validation and pitch/size arithmetic.
- EDID parsing and timing validation.
- Video-memory allocator.
- Rectangle, clipping, overlap, and ROP classification.
- Capability model.
- DirectDraw surface bookkeeping.
- Recovery and acceleration policy.

Host-build the pure portions of this core for unit and property testing on modern Windows.

### 3.2 Chipset backend interface

Define a small explicit interface rather than embedding S3 assumptions in common code:

- Detect device and revision.
- Read capabilities and VRAM size.
- Enter, validate, and leave a display mode.
- Map framebuffer and MMIO apertures.
- Wait for idle/FIFO space with a timeout.
- Reset or disable a wedged engine.
- Set palette and framebuffer start.
- Upload and position cursor.
- Solid fill.
- Screen-to-screen copy.
- CPU-to-screen transfer.
- Optional color-key and pattern operations.
- Read scanline/vblank state.

Unsupported operations must return a clear fallback result. The driver must never advertise an acceleration capability until its conformance tests pass.

### 3.3 Safety and recovery

All hardware waits require bounded timeouts. On timeout:

1. Record the operation and relevant register snapshot.
2. Attempt a documented engine reset once.
3. Disable the affected acceleration primitive for the session.
4. Fall back to DIB Engine or CPU rendering.
5. Preserve a usable desktop whenever possible.

Provide registry switches for disabling each acceleration family independently. Begin with every optional primitive disabled and enable them one at a time as tests pass.

## 4. Virtual-machine strategy

### 4.1 Primary VM: 86Box with S3 ViRGE/DX

This is the authoritative development VM because it exposes the target chipset rather than a paravirtual display interface.

Suggested initial configuration:

- Late-1990s Intel-compatible motherboard.
- Pentium II around 300 MHz.
- 128 MB RAM.
- S3 ViRGE/DX 86C375 with 4 MB VRAM.
- 2 GB IDE disk.
- IDE CD-ROM and floppy drives.
- One CPU, ACPI initially disabled if configurable.
- Network and sound disabled during driver bring-up.
- COM1 redirected for logging/debugging.

Maintain snapshots at:

1. Clean formatted disk.
2. Fresh Windows 98SE installation in standard VGA mode.
3. Toolchain and test utilities installed.
4. Immediately before each driver installation milestone.
5. Last known good driver build.

The user must supply lawfully licensed Windows 98SE installation media and any required product key. Do not download or commit operating-system media.

### 4.2 Secondary VM: VirtualBox reference system

Use VirtualBox only as a comparison and productivity environment:

- A VBoxVGA configuration can run the OS/2 Museum-style framebuffer driver.
- VBoxVGA, VBoxSVGA, or VMSVGA can run VMDisp9x/SoftGPU as a known behavior reference.
- Run the same GDI and DirectDraw test applications and compare externally visible results.
- Do not use VirtualBox success as evidence that S3 register-level code is correct.

Pin a known-working VirtualBox version for the reference image rather than silently updating it. Record the exact host version and VM configuration in the test manifest.

### 4.3 Optional QEMU VM

QEMU `std-vga` plus VMDisp9x is useful for scripted boot and application-level test development, and its GDB stub allows source-level debugging of guest code where 86Box's tooling is insufficient. It is not a substitute for the 86Box S3 target.

### 4.4 Hyper-V

Do not use Hyper-V as the primary guest platform. It does not provide the required S3-compatible adapter or a suitable Windows 9x synthetic display stack. Hyper-V may also affect the performance or availability of other hypervisors on a Windows host, so record whether it is enabled before installing VirtualBox or 86Box.

### 4.5 Modern CPU compatibility

Prepare a separate, reproducible installation-media patch step if Windows 98SE encounters modern CPU/TLB issues. Record every modified system file and its hash. The patch must remain a VM prerequisite, not part of the Velocity9x driver distribution.

### 4.6 Debugging tooling

Serial logging over COM1 is the primary diagnostic channel and must work from the skeleton driver onward. Beyond that:

- Use 86Box's built-in debugger/monitor for register-level and memory inspection against the emulated ViRGE.
- Use the QEMU GDB stub for source-level debugging of portable guest code where applicable.
- Treat Microsoft debuggers (WDEB386 and other DDK-supplied tools) as potentially license-encumbered; decide during the toolchain spike whether they may be used locally, and never redistribute them.

## 5. Build and repository design

### 5.1 Proposed repository layout

```text
velocity9x/
  PLAN.md
  README.md
  docs/
    specifications/
    decisions/
  include/
  src/
    common/
    display16/
    minivdd32/
    directdraw/
    chipsets/
      s3/
        virge/
  tools/
    diag/
    modes/
    package/
  tests/
    host/
    win9x/
    directdraw/
    hardware/
  packaging/
    win98se/
  scripts/
```

### 5.2 Toolchain spike

Evaluate Open Watcom as the primary compiler because it can produce 16-bit and 32-bit x86 targets and keeps more code in C. Limit assembly to ABI thunks, port I/O, and instructions the compiler cannot express safely.

The spike must answer:

- Can a minimal original 16-bit display `.DRV` be compiled and linked?
- Can a minimal original `.VXD` load and emit a serial message?
- How much logic must remain in the 16-bit display driver versus the 32-bit VXD, given the DIB Engine and DDI calling conventions?
- Which debugger arrangement is workable: 86Box's debugger, QEMU's GDB stub, serial-only, or a DDK debugger?
- Can the build run entirely from command-line scripts?
- Which DDK tools, libraries, headers, or import definitions are unavoidable?
- Can unavoidable licensed inputs remain external and unmodified?
- Are generated outputs free of embedded third-party redistributables?
- Can the complete build be reproduced from a documented tool manifest?

Record exact versions and hashes. Do not vendor Microsoft DDK files into the repository.

## 6. Development phases and gates

When a phase begins, record a timebox estimate in `docs/decisions/`. A blown timebox triggers a recorded re-scope decision, not a silent overrun.

### Phase 0 - Setup

Deliverables:

- Initial Windows 9x ABI specification covering the display driver, mini-VDD, and DirectDraw HAL boundaries, written from public documentation.

Exit gate:

- The specification is complete enough that Phase 1 work can be reviewed against it.
- No third-party source or binary is present in the implementation repository.

### Phase 1 - Reproducible toolchain and skeleton driver

Deliverables:

- Command-line build scripts.
- Minimal installable `.DRV`, `.VXD`, and INF produced from original code.
- Serial logging and build/version identification.
- Safe uninstall and standard-VGA recovery procedure.

Exit gate:

- A clean Win98SE VM can install, load, log, unload, and remove the skeleton for 20 consecutive cycles without corrupting the guest.

### Phase 2 - Device discovery and framebuffer bring-up

Deliverables:

- PCI identification for 5333:8A01 only.
- BAR/resource discovery and validation.
- VRAM-size detection with a safe override.
- Linear-framebuffer mapping.
- One conservative 640x480x8 mode, initially through a documented firmware interface if necessary.
- CPU/DIB Engine desktop rendering with no hardware acceleration.

Exit gate:

- The desktop is stable after reboot and mode re-entry.
- Pixel-format, pitch, bounds, and framebuffer tests pass.
- Unsupported devices are rejected without touching hardware.

### Phase 3 - Mode framework and basic usability

Deliverables:

- 640x480, 800x600, and 1024x768 at 8- and 16-bit depths permitted by VRAM.
- Verified determination of the ViRGE/DX's high-depth support (packed 24 bpp versus 32 bpp, and whether acceleration is available there) before any such mode is advertised.
- Palette handling for 8 bpp.
- Correct pitch and visible/off-screen memory accounting.
- Mode change, shutdown, restart, and standard-VGA fallback.
- Full-screen DOS transition tests, even if initially documented as unsupported.

Exit gate:

- The mode matrix passes repeated changes without corruption or leaked mappings.
- Invalid modes and insufficient-VRAM cases are rejected cleanly.

### Phase 4 - Hardware cursor

Deliverables:

- Cursor upload, show/hide, movement, clipping, and hotspot handling.
- Software-cursor fallback.
- A registry switch to disable the hardware cursor.

Exit gate:

- Cursor stress tests pass at every supported mode and screen edge.

### Phase 5 - Conservative GDI acceleration

Implementation plan: [docs/plans/gdi-acceleration.md](docs/plans/gdi-acceleration.md),
which splits this phase into builds `gdi-accel-000`..`005`, one primitive per
build. **Builds 000 through 003 are done**, which is items 1 to 3 of the list
below: solid fill, non-overlapping screen copy, and overlapping copy in all
eight directions all run on the engine on both S3 chips
([000](docs/decisions/2026-08-26-gdi-accel-000.md),
[001](docs/decisions/2026-08-26-gdi-accel-001.md),
[002](docs/decisions/2026-08-27-gdi-accel-002.md),
[003](docs/decisions/2026-08-27-gdi-accel-003.md)). Items 4 and 5 —
CPU-to-screen upload and extra ROPs — are not started.

One thing that phase discovered and this list did not anticipate: the `BitBlt`
export lives in the 16-bit layer all four families share, so *every* family got
the dispatcher, and the three with no 2D engine take its decline branch on every
blit permanently. The exit gate therefore has to be run on an engine-less family
as well, not only on the accelerated one.

Enable one primitive at a time:

1. Solid rectangle fill.
2. Non-overlapping screen-to-screen copy.
3. Overlapping copy in every direction.
4. CPU-to-screen upload.
5. Selected ROPs and transparent/color-key cases only after conformance testing.

Deliverables:

- Bounded FIFO/idle waits and engine reset.
- Per-primitive registry controls.
- Alignment, clipping, pitch, and overlap validation.
- Size thresholds selecting CPU or accelerator paths.
- DIB Engine fallback for every unsupported case.

Exit gate:

- Pixel-for-pixel comparison against a software reference passes randomized rectangles, pitches, clipping regions, overlap directions, and supported ROPs.
- Injected timeouts recover to an operable desktop.
- The comparison is not allowed to pass vacuously: the driver's own counters are read back, and a primitive that is advertised and enabled and never fired is a failure. A comparison harness that silently exercised the decline path on every operation would pass perfectly and prove nothing.

**Status after build 003: met, with one item recorded as incidental rather than counted as passed.** Randomized rectangles, overlap directions and supported ROPs are covered on every mode of both chips; injected timeouts recover to a rendering desktop, verified by observing the gate fail on a deliberately broken build; and clipping regions are covered by a two-band clip pass which showed that GDI *splits* a straddling blit into one driver call per clip rectangle rather than hiding clipping from the driver - 24 clipped operations produce exactly 48 accelerated ones. *Pitch coverage is incidental* - eleven distinct pitches across the mode list, but no variation within a mode. See [the 003 record](docs/decisions/2026-08-27-gdi-accel-003.md).

### Phase 6 - DirectDraw foundation

Deliverables:

- Conservative capability reporting.
- Primary surface creation and destruction.
- Surface lock/unlock with correct pitch and pointer semantics.
- System-memory and video-memory surface bookkeeping.
- Off-screen VRAM heap allocator.
- Surface-loss and display-mode-change behavior.

Exit gate:

- DirectDraw enumeration and surface-lifecycle tests pass without leaks, stale pointers, or unsupported claims.

### Phase 7 - DirectDraw acceleration

Implement in this order:

1. Blit to/from the primary surface.
2. Video-memory-to-video-memory blit.
3. Source and destination color key where hardware behavior is verified.
4. Back buffers and page flipping.
5. Flip status, blit status, scanline, and vertical-blank reporting.
6. 8-bit palette updates synchronized with DirectDraw surfaces.

Every callback needs a software fallback or an explicit unsupported result. Do not advertise overlays, stretch blits, alpha blending, or 3D.

Exit gate:

- The DirectDraw test suite passes on clean Windows 98SE across all advertised modes.
- Representative DirectDraw 2D applications run without corruption.
- Repeated create/blit/flip/destroy loops complete without VRAM loss or engine hangs.
- The same application tests are compared with a known-good reference VM.

### Phase 8 - Modern display support

Only after the fixed-mode DirectDraw milestone:

- DDC/EDID reading.
- EDID parsing and monitor-range checks.
- Safe timing generation.
- Widescreen modes such as 1280x720 and 1360x768 when clock, pitch, and VRAM constraints permit.
- Refresh-rate selection and explicit safe defaults.
- Custom-mode diagnostic tool with validation and rollback.

Exit gate:

- Invalid or out-of-range timings cannot be activated accidentally.
- A failed mode returns to the last known good mode or standard VGA.

### Phase 9 - Compatibility expansion

After S3 ViRGE/DX is stable:

- Windows 95 compatibility.
- Windows Me compatibility.
- Additional ViRGE revisions.
- S3 Trio backend using the common interface.
- Matrox Millennium/Millennium II backend.
- Physical ViRGE/DX testing.

Each chipset receives its own behavioral specification, backend tests, and capability mask. Do not generalize from the emulator without physical verification.

### Future phase - OpenGL and 3D

OpenGL begins only after the DirectDraw release is stable and licensed. Treat it as a separate design project. Candidate directions include a software ICD, Mesa integration under compatible terms, or a new hardware backend. No OpenGL dependency or ABI decision should constrain the initial 2D driver.

## 7. Test strategy

### 7.1 Host-side tests

Run modern native tests for code that does not depend on Windows 9x:

- Integer overflow in pitch and surface-size calculations.
- Rectangle clipping and overlap direction.
- ROP classification.
- EDID parsing and timing validation.
- VRAM allocation, fragmentation, coalescing, and exhaustion.
- Capability derivation.
- Command validation and timeout policy.

Use property-based or randomized tests for dimensions, coordinates, pitches, color depths, and allocation sequences.

### 7.2 Win98SE guest tests

Create original test applications for:

- GDI primitives and pixel-hash validation.
- Rapid mode changes.
- Palette animation.
- Cursor movement and shape churn.
- Window move, resize, scroll, and font rendering.
- DOS window/full-screen transitions.
- Suspend-like display disable/enable sequences.
- DirectDraw capability enumeration.
- Surface create/destroy/lock/lost/restore cycles.
- Blits, clipping, color keys, page flips, scanline, and vblank.

Tests should log machine-readable results to a virtual serial port or disk image. Visual screenshots are secondary evidence; pixel buffers and explicit status codes are primary.

### 7.3 Reliability gates

Before calling the DirectDraw milestone complete:

- 50 cold boots and 50 warm reboots without display-driver failure.
- 1,000 mode switches across the supported matrix.
- 10,000 randomized accelerated GDI operations with pixel comparison.
- 10,000 DirectDraw surface lifecycle iterations.
- 10,000 blit/flip iterations per advertised pixel format.
- Deliberate engine-timeout injection demonstrates recovery and fallback.
- Installation failure and uninstall always leave standard VGA recoverable.

The exact counts may be increased, but should not be reduced without a recorded decision.

## 8. Diagnostics and release artifacts

Every test build should expose:

- Build identifier and source revision.
- Detected PCI ID and revision.
- VRAM size and override status.
- Framebuffer/MMIO resources.
- Active mode, pitch, pixel format, and timing.
- Enabled/disabled acceleration primitives.
- DirectDraw capability mask and heap state.
- Timeout/reset counters.
- Last failed operation in a compact crash-safe record.

A release candidate should contain only:

- Driver binaries.
- INF and uninstall/recovery instructions.
- Diagnostic/test utilities written by the project.
- License and notices selected for Velocity9x.
- A machine-readable build manifest and hashes.

It must not contain Windows files, DDK files, firmware, third-party drivers, or reference source.

## 9. Risks

Recorded here so mitigations are deliberate rather than reactive:

1. **Toolchain viability (highest kill-risk).** Open Watcom may prove unable to produce a working 16-bit display `.DRV` or LE-format `.VXD` without encumbered DDK components. Mitigation: the toolchain spike runs before any binary-boundary or architecture commitment.
2. **Emulation fidelity.** 86Box's ViRGE model may differ from silicon in timing, FIFO behavior, or undocumented register semantics. Mitigation: program conservatively against documented behavior, avoid tuning to emulator quirks, and require physical verification in Phase 9 before claiming hardware support.
3. **Documentation scarcity.** DIB Engine internals and parts of the Win9x display DDI are thinly documented. Mitigation: write the ABI specification first and validate it empirically against the skeleton driver.
4. **High-depth mode support.** The ViRGE family's support above 16 bpp is generally packed 24 bpp rather than 32 bpp, and acceleration there is limited or absent. Mitigation: Phase 3 verifies actual hardware behavior before any high-depth mode is advertised; the release target treats depths above 16 bpp as conditional.
5. **Architecture split.** The intended thin-16-bit/heavy-32-bit split may be impossible under the real DDI, forcing more logic into the 16-bit driver than planned. Mitigation: treated as a spike question; the common core stays portable either way.
6. **Scope creep.** The deferred list (Section 2.2) and per-phase timeboxes are the controls; changes to either require a recorded decision.

## 10. Immediate next actions

1. Adopt this plan as the baseline and correct any scope decisions before coding.
2. Move all downloaded/reference material outside the implementation repository.
3. Create a clean Windows 98SE 86Box VM in standard VGA mode.
4. Create the separate VirtualBox reference VM only after the S3 VM is reproducible.
5. Write the first specifications: Win9x display ABI boundary, PCI/resource discovery, logging protocol, and framebuffer mode contract.
6. Complete the Open Watcom/toolchain spike before committing to binary boundaries.
7. Build and test the original skeleton driver.
8. Do not begin S3 acceleration until the software framebuffer path and recovery procedure are reliable.

## 11. Definition of the first public-ready milestone

Velocity9x reaches its first public-ready milestone when:

- Win98SE installs and recovers safely on the 86Box S3 ViRGE/DX target.
- Supported GDI and DirectDraw operations pass the stated conformance and reliability gates.
- Unsupported capabilities are not advertised.
- The build is reproducible without redistributing restricted toolchain inputs.
- The repository and release artifacts contain no third-party source, binaries, or restricted materials.
- The owner has selected and reviewed the distribution license.

## 12. Planning references

These references justify the planning decisions but are not implementation source:

- OS/2 Museum, *Windows 9x Video Minidriver HD+*: https://www.os2museum.com/wp/windows-9x-video-minidriver-hd/
- VMDisp9x project overview: https://github.com/JHRobotics/vmdisp9x
- SoftGPU project and VM compatibility matrix: https://github.com/JHRobotics/softgpu
- VBEMP project information and license: https://bearwindows.zcm.com.au/vbe9x.htm
- Background decision record: docs/decisions/2026-08-08-initial-direction.md
