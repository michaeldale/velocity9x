# Current status and next work

Reviewed against the checkout and recorded evidence on 2026-09-07. Update this
page when a default, validation result or next step changes; keep the dated
decision records as the history. [PLAN.md](../PLAN.md) is the original planning
baseline, not the current backlog.

## Release and checkout

The latest release is [0.7.0](../releases/0.7.0/README.md), built from
`8d7ef9e`. Its archive labels record host build/audit status, not a fresh run of
every archive on every card. The evidence below establishes particular features
on the named builds and machines; it does not certify every current binary.

| Feature | Released 0.7.0 | Current unreleased checkout | Evidence and limit |
|---|---|---|---|
| S3 GDI fill, copy and overlap | On by default at supported depths | Same defaults | [Physical Trio64 result](decisions/2026-08-27-crystalmark-barry-accelerated.md); 32-bpp drawing falls back to software |
| ViRGE monochrome upload | Implemented, off by default (`GdiAccelUpload=0`) | Same | [Build 004](decisions/2026-08-27-gdi-accel-004.md); Trio64 and colour uploads decline |
| GDI text acceleration | Software text only | Trio64 and ViRGE implementation, off by default (`GdiAccelText=0`) | [Build 005](decisions/2026-09-06-gdi-accel-005-text.md): 86Box mode matrices pass; physical ViRGE/DX passes; physical Trio64 remains unverified after its hang |
| DOS-box ADVFUNC shield | Absent | Included by default in the mini-VDD | [Shipping record](decisions/2026-09-06-advfunc-shield-ships.md): traced variant fixed 6/6 physical Trio64 trials; shipping form passed ViRGE/86Box checks but still needs physical Trio64 verification |
| Hardware Direct3D | Default on ViRGE/DX and Trio3D/2X; matching 5:5:5 selected automatically | Same | [0.7.0 results](../CHANGELOG.md#070---2026-09-05); Trio3D uses bilinear instead of two-pass trilinear, and blend faults remain open |
| Software Direct3D | Opt-in in the S3, ATI and VBE packages through the settings page or `Direct3D=2` | Same defaults; exact incremental edges reduce small-triangle cost | [86Box timings and unchanged pixel results](decisions/2026-09-07-software-rasterizer-edge-stepping.md): about 2.2x for the synthetic small-triangle scene; no physical or game-speed claim |

Software mode includes depth testing, Gouraud shading, point/bilinear sampling,
ARGB1555/ARGB4444/RGB565 textures, WRAP/CLAMP and vertex-alpha blending. It does
not implement texture alpha, perspective correction, mip selection or fog.
The [alpha](decisions/2026-09-02-software-alpha-blending.md),
[wrap](decisions/2026-09-02-software-texture-wrap.md) and
[RGB565](decisions/2026-09-02-software-rgb565-textures.md) records distinguish
the software results from hardware controls. Changing Direct3D mode requires
a restart. Without an S3D engine, the default advertises no Direct3D until
Software is selected.

## Target coverage

| Target | Recorded coverage | Boundary |
|---|---|---|
| S3 ViRGE/DX | Extensive 86Box regression; [physical ViRGE GDI/text and DOS-box checks](decisions/2026-09-06-advfunc-shield-ships.md) | Recent physical GDI success does not establish physical 3D throughput |
| S3 Trio3D/2X (`5333:8A13`) | Physical hardware Direct3D: Final Reality and 3DMark 99 complete in [0.7.0](../CHANGELOG.md#070---2026-09-05); [generic VBE also tested](decisions/2026-09-02-vbe-tier0-on-a-real-trio3d.md) | Uses the S3D backend with measured chip-specific restrictions; no matching Trio3D emulator validation |
| S3 Trio64 / Trio32 | Trio64 86Box regression, [physical PCI GDI](decisions/2026-08-27-crystalmark-barry-accelerated.md), [physical VLB Win95 bring-up](handoffs/2026-08-22-vlb-manual-select-handover.md), [Trio32 guest](decisions/2026-08-29-s3-trio32-alias-guest.md) | VLB Win95 omits the mini-VDD. Text remains off. Trio32 BIOS refuses 800x600x32 |
| S3 aliases `8810`, `8812`, `8813`, `8814`, `8901` | Bound to the Trio64 path by the manifest | [Installation aliases, not validated targets](decisions/2026-08-29-s3-device-id-survey.md); `8811` is also shared by Trio32 and Trio64V+ |
| ATI Mach64 / Rage | Mach64 VT2 emulator bring-up | [Mach64 16-bpp scanout is wrong](issues/2026-08-16-tier0-defects-deferred.md); no physical ATI validation or native 2D backend |
| Generic VESA | QEMU/86Box plus [physical GMA 950](issues/2026-08-27-netbook-gma950-findings.md) and physical Trio3D | Dynamic BIOS modes, software drawing; Have-Disk permits unlisted cards, whose behavior still needs measurement |
| Matrox Millennium II | Historical physical and 86Box software-GDI passes with the stock Matrox mini-VDD | [Guarded mixed-pair boundary](specifications/matrox-millennium2-bringup.md); no claim that the current archive or replacement mini-VDD is physically validated |

DirectDraw surface allocation and VGA-port vblank services are shared by the
S3, ATI and VBE packages. The guarded Matrox candidate packages the display
driver and settings page without `V9XHAL.DLL`; its historical mixed-pair result
establishes software GDI only.
Hardware primary page flipping is S3-only: the
[HAL](../src/display32/ddhal_core.c) declines it without a native display-start
capability. A successful surface allocation or vblank probe does not establish
hardware flipping on ATI or VBE. All targets use software cursors.

## Open work, in order

| Priority | Next step | Acceptance evidence |
|---|---|---|
| Close Trio64 shield/text validation | On BARRY, start with text off and the shipping shield installed; run the bounded text probe, DOS-box cycles and acceleration harness before deciding the text default | [Text issue](issues/2026-09-06-text-acceleration-hangs-physical-trio64.md) and [DOS-box issue](issues/2026-09-06-dos-box-doubles-the-desktop-on-physical-trio64.md) updated with the exact binaries and results |
| Make desktop/DOS transitions dependable | Finish the remaining physical fullscreen-DOS return and live-mode repaint investigations; keep scanout checks independent of GDI readback | [Fullscreen DOS](issues/2026-08-28-fullscreen-dos-scanout.md), [BARRY repaint](issues/2026-08-20-live-mode-switch-no-repaint-barry.md), [VBE scanout](issues/2026-08-27-vbe-1024x768x16-scanout-stripes.md) |
| Close Direct3D compatibility holes | Reproduce the target switch in the emulator; identify Incoming's requested texture format before adding one; isolate Trio3D blend state | [SetRenderTarget](issues/2026-09-05-setrendertarget-is-accepted-and-ignored.md), [Incoming](issues/2026-09-05-incoming-refuses-the-hal-texture-formats.md), [latest blend investigation](decisions/2026-09-05-a-register-capture-of-both-trio3d-blend-states.md) |
| Make software Direct3D practical | Edge stepping and an 86Box RAM/aperture benchmark are done; next measure mixed texture/Z residency, optimize textured pixels and run a named-game gate | [Measured first scalar change](decisions/2026-09-07-software-rasterizer-edge-stepping.md), [remaining scalar plan](plans/software-rasterizer-scalar-fixes.md); physical timings still needed before residency policy, hybrid assistance or [SMP workers](plans/software-d3d-smp-workers.md) |
| Add Voodoo3 as the next native family | Physical survey and 86Box setup, then tier-0, 2D and D3D with a named-game gate | [Voodoo3 plan](plans/3dfx-voodoo3-family.md); the shared D3D core/engine split is already complete |

MTRR write-combining remains inspect-only; writing registers needs the physical
evidence specified in its [Stage A record](decisions/2026-08-28-mtrr-stage-a-inspect-only.md).
Hardware cursor work remains proposed. Synthetic vblank is not a current
tier-0 gap: the HAL already reads the VGA vblank status bit. See the corrected
[tier-0 plan](plans/tier0-quality.md).

The A8U4I5 framebuffer read-after-write hard lock reproduced under Microsoft's
driver too; it is tracked as a [board/card investigation](issues/2026-09-06-a8u4i5-trio64-hard-locks-on-framebuffer-readback.md),
not evidence that the shield or default acceleration causes that failure.
