# Decision records

A decision record is a dated finding or choice, with the evidence that produced it. Records are never edited after the fact; a later record supersedes an earlier one. Some records have sidecar evidence files (`.txt`, `.ini`) beside them with the same date prefix. Titles are listed by topic; open the record for the finding.

## Project direction and licensing

- [2026-08-08 Open-Source Windows 9x VGA Driver Discussion](2026-08-08-initial-direction.md)
- [2026-08-08 Phase 1 timebox and first implementation slice](2026-08-08-phase-1-timebox.md)
- [2026-08-08 Phase 2 portable resource groundwork timebox](2026-08-08-phase-2-timebox.md)

## Toolchain and build

- [2026-08-08 DDI and mini-VDD link spike](2026-08-08-ddi-link-spike.md)
- [2026-08-08 Consolidated driver-stage lifecycle result](2026-08-08-driver-stage-probe.md)
- [2026-08-08 Toolchain spike 1: portable core and Win16 NE image](2026-08-08-toolchain-spike-1.md)
- [2026-08-08 Dynamic VxD lifecycle probe result](2026-08-08-vxd-lifecycle-probe.md)
- [2026-08-16 The 32-bit engine dispatch vtable and the ddhal.c split](2026-08-16-engine32-vtable.md)
- [2026-08-16 Engine fault injection](2026-08-16-engine-fault-injection.md)
- [2026-08-16 Per-family packaging](2026-08-16-per-family-packaging.md)
- [2026-08-16 Multi-chip restructure baseline](2026-08-16-restructure-baseline.md)
- [2026-08-16 The S3 family merge](2026-08-16-s3-family-merge.md)

## Win16 display driver and DDI

- [2026-08-08 Active 640x480x8 bring-up candidate](2026-08-08-active-640-candidate.md)
- [2026-08-09 800x600 boot fallback investigation](2026-08-09-800x600-boot-fallback-investigation.md)
- [2026-08-09 Fixed 800x600x8 bring-up candidate](2026-08-09-active-800-candidate.md)
- [2026-08-09 Phase 3 mode matrix](2026-08-09-phase-3-mode-matrix.md)
- [2026-08-09 Trace8b Phase 3 mode-matrix result](2026-08-09-trace8b-mode-matrix.md)
- [2026-08-09 Trace8c clock, palette, and repeat-matrix result](2026-08-09-trace8c-clock-palette-repeat.md)
- [2026-08-10 Display Properties settings page](2026-08-10-display-properties-settings-page.md)
- [2026-08-10 Dynamic mode switching (build modesw-1)](2026-08-10-dynamic-mode-switching.md)
- [2026-08-11 Monitor power management boundary (2026-08-11)](2026-08-11-monitor-power-management.md)
- [2026-08-14 S3 Trio64 conservative framebuffer bring-up](2026-08-14-trio64-bringup.md)
- [2026-08-20 32-bpp and 1280x1024 mode matrix, both S3 chips](2026-08-20-high-depth-mode-matrix.md)

## GDI acceleration

- [2026-08-26 GDI acceleration, build 000: a provably free decline path](2026-08-26-gdi-accel-000.md)
- [2026-08-26 GDI acceleration, build 001: the first accelerated drawing operation](2026-08-26-gdi-accel-001.md)
- [2026-08-27 CrystalMark Retro on BARRY: the accelerated run](2026-08-27-crystalmark-barry-accelerated.md)
- [2026-08-27 CrystalMark Retro on BARRY: the driver-0.5 baseline](2026-08-27-crystalmark-barry-baseline.md)
- [2026-08-27 GDI acceleration, build 002: non-overlapping screen-to-screen copy](2026-08-27-gdi-accel-002.md)
- [2026-08-27 GDI acceleration, build 003: overlap, and the Phase 5 exit gate](2026-08-27-gdi-accel-003.md)
- [2026-08-27 GDI acceleration, build 004: monochrome CPU-to-screen upload](2026-08-27-gdi-accel-004.md)
- [2026-08-27 GDI acceleration, build 004: what "CPU-to-screen upload" should actually be](2026-08-27-gdi-accel-004-design.md)
- [2026-08-27 GDI acceleration: what comes after the merge](2026-08-27-gdi-accel-next-steps.md)
- [2026-09-06 CrystalMark Retro on the 86Box Trio64 guest: text on against text off](2026-09-06-crystalmark-86box-trio64-text.md)
- [2026-09-06 GDI acceleration, build 005: text on the Trio64](2026-09-06-gdi-accel-005-text.md)

## DirectDraw HAL

- [2026-08-11 DirectDraw HAL (builds ddhal-1/ddhal-2)](2026-08-11-directdraw-hal.md)
- [2026-08-14 ViRGE DirectDraw blitter (build `virge-blt-003`)](2026-08-14-virge-blitter.md)
- [2026-08-24 Making `src/chipsets` HAL-free, ahead of a future NT driver](2026-08-24-chipset-hal-free-split.md)

## Direct3D hardware (ViRGE, Trio3D)

- [2026-08-11 Direct3D phase 2: device and context foundation](2026-08-11-direct3d-phase2.md)
- [2026-08-11 Direct3D phase 3: first hardware triangle](2026-08-11-direct3d-phase3.md)
- [2026-08-11 ViRGE engine foundation (engine-1)](2026-08-11-virge-engine-foundation.md)
- [2026-08-30 Turning Direct3D off works from a settings-page control, and only a restart hides the HAL device](2026-08-30-d3d-mode-disabled-gate.md)
- [2026-08-30 DDBLT_DEPTHFILL is one solid fill, and it is a trade-off rather than a win](2026-08-30-ddblt-depthfill.md)
- [2026-08-30 Depth testing was inert because it reserved 18 FIFO slots and the chip reports 16](2026-08-30-virge-depth-fifo-reservation.md)
- [2026-08-30 Four people shipped GL on the ViRGE and none of them wrote an ICD; the two walls are triangle setup and multiplicative blend](2026-08-30-virge-opengl-prior-art.md)
- [2026-09-02 A 5:5:5 desktop needs three places to agree, and finding the third took a day](2026-09-02-a-555-desktop-needs-three-places-to-agree.md)
- [2026-09-02 Final Reality on a real Trio3D: 3.21 overall, rendered blind](2026-09-02-final-reality-on-a-real-trio3d.md)
- [2026-09-02 The S3D engine writes ZRGB1555 because that is its only 16-bit destination](2026-09-02-s3d-writes-1555-because-it-can-only-write-1555.md)
- [2026-09-02 The Trio3D/2X runs the ViRGE path, with hardware Direct3D](2026-09-02-trio3d-on-the-s3-path.md)
- [2026-09-02 The VBE tier-0 driver runs a real S3 Trio3D, and the software rasterizer with it](2026-09-02-vbe-tier0-on-a-real-trio3d.md)
- [2026-09-03 A flip is not done when its registers are written](2026-09-03-a-flip-is-not-done-when-its-registers-are-written.md)
- [2026-09-03 Colour key and blend on the ViRGE: what 3DMark 99 asked for](2026-09-03-colour-key-and-blend-on-the-virge.md)
- [2026-09-03 Final Reality's black wedges on the ViRGE were the texture wrap bit](2026-09-03-final-reality-wedges-were-the-wrap-bit.md)
- [2026-09-03 The probe matrix, and the 3D-done bit it made necessary](2026-09-03-the-probe-matrix-and-the-3d-done-bit.md)
- [2026-09-03 The Trio3D reads the texture stride from the 3D stride register](2026-09-03-the-trio3d-reads-the-texture-stride.md)
- [2026-09-03 Two hypotheses on the Trio3D's alpha and mip, and what testing them left behind](2026-09-03-two-hypotheses-on-the-trio3d-and-what-they-left.md)
- [2026-09-04 A blended UNLIT draw loses its texture colour on the Trio3D/2X](2026-09-04-an-unlit-blend-loses-its-texture-on-the-trio3d.md)
- [2026-09-04 No encoding of the S3D alpha field blends on the Trio3D/2X](2026-09-04-no-encoding-of-the-alpha-field-blends-on-the-trio3d.md)
- [2026-09-04 A census of command words, and what 3DMark 99 is actually made of](2026-09-04-the-command-word-census.md)
- [2026-09-04 The halfa cells were right: partial alpha is wrong, and the curve rung hid it](2026-09-04-the-halfa-cells-were-right.md)
- [2026-09-04 The idle wait learns whether the part has a 3D-done bit](2026-09-04-the-idle-wait-learns-the-done-bit.md)
- [2026-09-04 The mip ladder: level selection is right, and trilinear is the alpha defect](2026-09-04-the-mip-ladder.md)
- [2026-09-04 The ramp: a sprite as an application draws one, and the fault's exact shape](2026-09-04-the-ramp-and-the-shape-of-the-blend-fault.md)
- [2026-09-04 The trilinear two-pass, and a retraction: the Trio3D/2X does blend](2026-09-04-the-trilinear-two-pass-and-a-retraction.md)
- [2026-09-04 The Trio3D runs the matrix: every cell but the blended ones, and no 3D-done bit](2026-09-04-the-trio3d-runs-the-matrix.md)
- [2026-09-04 Trilinear degrades to bilinear where the part cannot blend](2026-09-04-trilinear-degrades-where-the-part-cannot-blend.md)
- [2026-09-04 What the Trio3D's blend does with its operands: nothing with the source](2026-09-04-what-the-trio3d-blend-does-with-its-operands.md)
- [2026-09-05 A device on the primary chain works; a blend onto it draws nothing](2026-09-05-a-blend-onto-the-primary-chain-draws-nothing.md)
- [2026-09-05 A power cycle does not clear the Trio3D's blend state](2026-09-05-a-power-cycle-does-not-clear-the-trio3d-blend-state.md)
- [2026-09-05 A register capture of both Trio3D blend states, and what it does not reach](2026-09-05-a-register-capture-of-both-trio3d-blend-states.md)
- [2026-09-11 D3DSoftSysMem buys Final Reality nothing, because its textures never go there](2026-09-11-d3dsoftsysmem-buys-final-reality-nothing.md)

## Direct3D software rasterizer

- [2026-08-30 A Trio64 created a Direct3D device and drew, and the ViRGE ladder did not move](2026-08-30-software-d3d-path-proven.md)
- [2026-09-01 The probe now asks the surface what its colours are, and six ViRGE keys go red](2026-09-01-probe-derives-expected-colours.md)
- [2026-09-01 The software rasterizer draws triangles and depth-tests them, and the probe's expected colours are the ViRGE's](2026-09-01-software-rasterizer-depth.md)
- [2026-09-01 The software engine textures, and its caps say only what it does](2026-09-01-software-textures-and-caps.md)
- [2026-09-02 The software engine blends, with the four factors the ViRGE publishes](2026-09-02-software-alpha-blending.md)
- [2026-09-02 The software mode was unreachable from the page on every card it was written for](2026-09-02-software-mode-reaches-its-own-audience.md)
- [2026-09-02 The software rasterizer publishes RGB565; the ViRGE path cannot](2026-09-02-software-rgb565-textures.md)
- [2026-09-02 The software engine tiles textures, by dividing before it multiplies](2026-09-02-software-texture-wrap.md)
- [2026-09-07 Exact edge stepping cuts the cost of small software triangles](2026-09-07-software-rasterizer-edge-stepping.md)
- [2026-09-10 Three scalar fixes to the software rasterizer, and what the first attempt taught about calls](2026-09-10-rasterizer-scalar-fixes.md)
- [2026-09-10 The sampler's per-pixel work was mostly setup, and a nested lerp cannot pay for itself](2026-09-10-rasterizer-texel-units-and-bilinear.md)
- [2026-09-10 The software engine can sample a system-memory texture, behind a setting](2026-09-10-software-d3d-system-memory-textures.md)
- [2026-09-10 The render-target switch works, and two "driver defects" were one uncleared depth buffer](2026-09-10-the-render-target-switch-and-the-uncleared-depth-buffer.md)
- [2026-09-11 A texture with no pixel format is in the display's format](2026-09-11-a-texture-with-no-pixel-format-is-in-the-displays-format.md)
- [2026-09-11 The lightmap pass now draws, instead of being skipped](2026-09-11-the-lightmap-pass-now-draws.md)
- [2026-09-11 The software engine drew a blend it cannot express, opaque](2026-09-11-the-software-engine-drew-an-inexpressible-blend.md)

## VBE generic family and dynamic modes

- [2026-08-16 The VBE tier-0 family](2026-08-16-vbe-tier0-family.md)
- [2026-08-20 VBE mode inventory per target](2026-08-20-vbe-mode-inventory.md)
- [2026-08-28 Pineview lists 36 VBE modes and describes 6](2026-08-28-pineview-vbe-mode-list.md)

## S3 hardware baselines

- [2026-08-20 The survey at schema 2: what a VLB machine forced, and what it has not yet said](2026-08-20-vlb-survey-schema2.md)
- [2026-08-21 The VLB linear aperture works, at both addresses, and the driver already does enough](2026-08-21-vlb-aperture-answered.md)
- [2026-08-26 The dynamic VBE pipeline is inert on physical S3 silicon](2026-08-26-s3-physical-pipeline-inert.md)
- [2026-08-28 Write-combining the aperture, Stage A: read the registers, decide, write nothing](2026-08-28-mtrr-stage-a-inspect-only.md)
- [2026-08-29 The Trio64V+ and Trio32 already publish 8811, so the driver has been binding them all along](2026-08-29-s3-device-id-survey.md)
- [2026-08-29 86Box's Trio32 publishes 8811, so the shipping driver already drove it - and its BIOS is not the Trio64's](2026-08-29-s3-trio32-alias-guest.md)

## ATI

- [2026-08-16 ATI Mach64 / Rage Mobility hardware audit](2026-08-16-ati-mach64-hardware-audit.md)

## Matrox

- [2026-08-09 Matrox Millennium II physical baseline](2026-08-09-millennium2-physical-baseline.md)
- [2026-08-11 Millennium II Candidate 8 86Box activation](2026-08-11-millennium2-86box-candidate8.md)
- [2026-08-11 Millennium II Candidate 8 physical activation](2026-08-11-millennium2-physical-candidate8.md)
- [2026-08-11 Millennium II Candidate 9 physical activation](2026-08-11-millennium2-physical-candidate9.md)
- [2026-09-09 The Millennium 2064W puts its framebuffer in BAR1, not BAR0](2026-09-09-millennium-2064w-bar-ordering.md)
- [2026-09-10 A Millennium guest in an hour: an inbox driver, a candidate that loads, and a stage name that lied](2026-09-10-the-2064w-in-a-guest.md)
- [2026-09-10 The 2064W's own BIOS answers the question, and a guarded candidate now exists](2026-09-10-the-2064w-is-drivable-by-the-vbe-path.md)
- [2026-09-11 The 2064W's aperture opens with mgamode, and the card has 8 MiB](2026-09-11-the-2064w-aperture-opens-with-mgamode.md)

## Intel GMA

- [2026-08-17 Intel GMA Gen3 (915/945/G33) hardware audit](2026-08-17-intel-gma-gen3-hardware-audit.md)
- [2026-08-17 intel-gma Phase 0 evidence - the DOS half, measured](2026-08-17-intel-gma-phase0-dos-evidence.md)
- [2026-08-17 intel-gma Phase 0 evidence - the Windows half, measured](2026-08-17-intel-gma-phase0-windows-evidence.md)
- [2026-09-12 Intel Phase 1 is code-ready, not hardware-complete](2026-09-12-intel-phase1-readonly-mmio-implementation.md)
- [2026-09-12 Intel Phase 1 measured on the netbook: the fingerprint passes, and the offsets are confirmed](2026-09-12-intel-phase1-physical-capture.md)
- [2026-09-12 Intel Phase 2 measured on the netbook: the VBIOS maps the whole aperture into stolen memory, identically on two cold boots](2026-09-12-intel-phase2-gtt-inventory.md)
- [2026-09-12 Intel Phase 3 event-matrix implementation is ready for physical measurement](2026-09-12-intel-phase3-event-matrix-implementation.md)
- [2026-09-12 Intel Phase 3 measured on the netbook: no firmware event moves ownership, and the takeover point is quiet](2026-09-12-intel-phase3-event-matrix.md)
- [2026-09-12 Intel Phase 4: the 945GSE internal-buffer erratum keeps the write gate closed](2026-09-12-intel-phase4-errata-gate.md)
- [2026-09-12 The 945 "Intel Flush Page": what it is, and what it does not explain](2026-09-12-intel-flush-page-lead.md)
- [2026-09-13 The XP miniport across the erratum-fix boundary: 4864 removes render-clock switching; erratum 12 is not visible in strings](2026-09-13-intel-xp-miniport-diff-across-the-erratum-fix.md)
- [2026-09-13 Intel Phase 4: the write gate opens on a risk decision, not on an erratum workaround](2026-09-13-intel-phase4-gate-opened-by-risk-decision.md)
- [2026-09-14 A CPU write to stolen memory does not stick; the GMADR aperture is the path](2026-09-14-cpu-writes-to-stolen-memory-need-the-aperture.md)
- [2026-09-14 Intel Phase 4 measured: the ring accepts commands and the GPU executed a blit](2026-09-14-intel-phase4-first-write-the-gpu-executed-a-blit.md)
- [2026-09-17 Intel documents the 945's fill rule: top-left, for D3D and OpenGL alike](2026-09-17-intel-gen3-fill-rule-documented-by-intel.md)
- [2026-09-17 The 945GSE graphics device has exactly one PCI ID; the wider Gen3 IDs are a different decision](2026-09-17-intel-945gse-pci-id-survey.md)
- [2026-09-17 27A6 and 2776 are claimed as part of the 945GME/GSE family, by decision](2026-09-17-intel-945-function1-ids-claimed-by-decision.md)
- [2026-09-18 Intel runtime Direct3D and the Intel flip are on by default; the arm-file keys become off switches](2026-09-18-intel-runtime-3d-and-flip-on-by-default.md)
- [2026-09-18 Gen3 page flip audit against i915 v4.4: the flip-pending bits are 11 and 10, not 2 and 6, and a plane-base write is a pending flip too](2026-09-18-intel-gen3-page-flip-audit.md)

## NVIDIA and other surveyed cards

- [2026-09-11 The GD5430 reads its own memory size, and its BIOS mode table needs reading carefully](2026-09-11-cirrus-gd5430-memory-and-mode-table.md)
- [2026-09-11 Two NVIDIA cards baselined, and what the VGA ports are not hiding](2026-09-11-nvidia-nv4-and-nv5-baselines.md)

## DOS box and mini-VDD

- [2026-08-18 Per-family gating of the mini-VDD's boot-time VBE collection](2026-08-18-minivdd-vbe-collect-gating.md)
- [2026-08-28 The DOS-box fault is on the way out, and it is a ninth-dot artefact](2026-08-28-dos-box-exit-ninth-dot.md)
- [2026-08-29 The VDD never asks about banking, so the virtualization hypothesis has no support](2026-08-29-dos-box-banking-not-asked.md)
- [2026-08-29 Tier-0 breaks the same way, on a different chip, and the stock driver does not](2026-08-29-dos-box-exit-tier0.md)
- [2026-08-29 The VDD reserves no off-screen memory, and telling it the truth does not change that](2026-08-29-dos-box-vdd-reservation.md)

## Test infrastructure and VMs

- [2026-08-17 Velocity9x against the retail S3 driver, Ironfield RTS](2026-08-17-native-driver-benchmark.md)

## Releases

- [2026-09-06 The mini-VDD swallows a DOS box's write to ADVFUNC_CNTL, in every build](2026-09-06-advfunc-shield-ships.md)

## Other records

- [2026-08-29 The Direct3D block splits into a chip-neutral core and one engine, and the probe cannot tell](2026-08-29-d3d-core-engine-split.md)
- [2026-09-04 Four megabytes is what picks the resolution, and at 800x600 mipmapping is off](2026-09-04-four-megabytes-is-the-resolution-limit.md)
- [2026-09-11 CR36 and the aperture base, confirmed on a physical ViRGE/DX](2026-09-11-virge-dx-registers-confirmed-on-silicon.md)
