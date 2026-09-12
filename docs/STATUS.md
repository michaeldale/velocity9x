# Current status and next work

Reviewed against the checkout and recorded evidence on 2026-09-12. Update this
page when a default, validation result or next step changes; keep the dated
decision records as the history. [PLAN.md](../PLAN.md) is the original planning
baseline, not the current backlog.

## Release and checkout

The latest release is [0.7.1](../releases/0.7.1/README.md), built from `977993e`
and published. Its archive labels record host build/audit status, not a fresh
run of every archive on every card. The evidence below establishes particular
features on the named builds and machines; it does not certify every current
binary.

Since that build the checkout carries two packaging changes and one code change.
The Matrox candidate's 2064W mode list widened from three modes to nine, and
CRTCEXT3 bit 7 is deliberately still not written
([record](decisions/2026-09-11-the-2064w-aperture-opens-with-mgamode.md)). The
VBE package now reads an absent `Direct3D` key as the software rasterizer
rather than as the chip's own engine, which on that family is the difference
between the CPU rasterizer and no Direct3D at all
([record](decisions/2026-09-12-vbe-defaults-to-the-software-rasterizer.md)); the
code change is the per-family default behind it, which no other family sets.
Everything else below is released.

| Feature | Released 0.7.1 | Current checkout | Evidence and limit |
|---|---|---|---|
| S3 GDI fill, copy and overlap | On by default at supported depths | Same defaults | [Physical Trio64 result](decisions/2026-08-27-crystalmark-barry-accelerated.md); 32-bpp drawing falls back to software |
| ViRGE monochrome upload | Implemented, off by default (`GdiAccelUpload=0`) | Same | [Build 004](decisions/2026-08-27-gdi-accel-004.md); Trio64 and colour uploads decline |
| GDI text acceleration | Trio64 and ViRGE implementation, off by default (`GdiAccelText=0`) | Same | [Build 005](decisions/2026-09-06-gdi-accel-005-text.md): 86Box mode matrices pass; physical ViRGE/DX passes; physical Trio64 remains unverified after its hang |
| DOS-box ADVFUNC shield | Included by default in the mini-VDD | Same | [Shipping record](decisions/2026-09-06-advfunc-shield-ships.md): traced variant fixed 6/6 physical Trio64 trials; shipping form passed ViRGE/86Box checks but still needs physical Trio64 verification |
| Hardware Direct3D | Default on ViRGE/DX and Trio3D/2X; matching 5:5:5 selected automatically | Same | [0.7.0 results](../CHANGELOG.md#070---2026-09-05); Trio3D uses bilinear instead of two-pass trilinear, and blend faults remain open. The 0.7.1 texture-format fix changed this path too and **has not been run on a ViRGE** |
| Software Direct3D | Opt-in in the S3 and ATI packages through the settings page or `Direct3D=2`, and **on by default in the VBE package** since 2026-09-12 ([record](decisions/2026-09-12-vbe-defaults-to-the-software-rasterizer.md)), where the alternative is no Direct3D at all - it takes effect only at 16 bpp, and that family's default mode is 8 bpp; exact incremental edges, an exact divide-by-255, a single colour clamp, per-span format/compare dispatch, a per-triangle sampler, DESTCOLOR blending, and system-memory textures behind `D3DSoftSysMem=1` | Same | [Edge timings](decisions/2026-09-07-software-rasterizer-edge-stepping.md) about 2.2x for the synthetic small-triangle scene, then [scalar fixes](decisions/2026-09-10-rasterizer-scalar-fixes.md) and [sampler fixes](decisions/2026-09-10-rasterizer-texel-units-and-bilinear.md) reaching 1.75x point-sampled, 1.53x bilinear, 1.48x depth-tested and 1.41x alpha-blended in RAM and about half those in emulated VRAM; pixel hashes unchanged; no physical or game-speed claim |

Software mode includes depth testing, Gouraud shading, point/bilinear sampling,
ARGB1555/ARGB4444/RGB565 textures, WRAP/CLAMP and vertex-alpha blending. It does
not implement texture alpha, perspective correction, mip selection or fog.
A texture that names no pixel format is in the display's, and is classified
as such rather than refused - until 2026-09-11 both engines refused it, which
dropped every texture Final Reality created and drew its scenes untextured
([record](decisions/2026-09-11-a-texture-with-no-pixel-format-is-in-the-displays-format.md)).
Its blend factors are the four S3's own driver publishes - ONE and SRCALPHA
for source, ZERO and INVSRCALPHA for destination - plus DESTCOLOR as a source
factor, which the S3D unit cannot express and a CPU rasterizer gets for one
product per channel, so the multiplicative lightmap pass draws
([record](decisions/2026-09-11-the-lightmap-pass-now-draws.md)). A pair
outside those five draws nothing and is counted, rather than drawing opaque
and painting over the frame
([record](decisions/2026-09-11-the-software-engine-drew-an-inexpressible-blend.md)).
The hardware path still publishes and implements the original four.
`D3DSoftSysMem=1` lets the engine sample a texture through the surface's own
linear address instead of the aperture; it is off by default and buys Final
Reality nothing, because DirectDraw never puts that application's textures in
system memory
([record](decisions/2026-09-11-d3dsoftsysmem-buys-final-reality-nothing.md)).
The probe cell that creates a texture without naming a format, `DisplayFmtTex*`,
carries no pass/fail key: its own draw writes nothing while the application
renders the same kind of texture correctly, and the disagreement is unexplained.
Rasterizer timings now come from two guests - the Pentium MMX 200
`Win98SE-Trio64` and `Win98SE-Fast-D3D` on port 9878, a Celeron 533 whose
measurements put the bound on the aperture rather than the CPU
([guests](vm-environment.md)).
The [alpha](decisions/2026-09-02-software-alpha-blending.md),
[wrap](decisions/2026-09-02-software-texture-wrap.md) and
[RGB565](decisions/2026-09-02-software-rgb565-textures.md) records distinguish
the software results from hardware controls. Changing Direct3D mode requires
a restart. Without an S3D engine, the default advertises no Direct3D until
Software is selected.

## Target coverage

| Target | Recorded coverage | Boundary |
|---|---|---|
| S3 ViRGE/DX | Extensive 86Box regression; [physical ViRGE GDI/text and DOS-box checks](decisions/2026-09-06-advfunc-shield-ships.md); [CR36, CR59/CR5A and a 32-bit aperture read off a physical card](decisions/2026-09-11-virge-dx-registers-confirmed-on-silicon.md) | Recent physical GDI success does not establish physical 3D throughput, the ZRGB1555-into-RGB565 mismatch is still unanswered, and 0.7.1's [texture-format change](decisions/2026-09-11-a-texture-with-no-pixel-format-is-in-the-displays-format.md) to the hardware path has not been run on this chip in either a guest or silicon |
| S3 Trio3D/2X (`5333:8A13`) | Physical hardware Direct3D: Final Reality and 3DMark 99 complete in [0.7.0](../CHANGELOG.md#070---2026-09-05); [generic VBE also tested](decisions/2026-09-02-vbe-tier0-on-a-real-trio3d.md) | Uses the S3D backend with measured chip-specific restrictions; still no emulator validation, though the reason is no longer that no profile exists - 86Box build 9001 carries `trio3d2x_agp` with an 8 MiB ROM, unbooted here ([guests](vm-environment.md)) |
| S3 Trio64 / Trio32 | Trio64 86Box regression, [physical PCI GDI](decisions/2026-08-27-crystalmark-barry-accelerated.md), [physical VLB Win95 bring-up](handoffs/2026-08-22-vlb-manual-select-handover.md), [Trio32 guest](decisions/2026-08-29-s3-trio32-alias-guest.md) | VLB Win95 omits the mini-VDD. Text remains off. Trio32 BIOS refuses 800x600x32 |
| S3 aliases `8810`, `8812`, `8813`, `8814`, `8901` | Bound to the Trio64 path by the manifest | [Installation aliases, not validated targets](decisions/2026-08-29-s3-device-id-survey.md); `8811` is also shared by Trio32 and Trio64V+ |
| ATI Mach64 / Rage | Mach64 VT2 emulator bring-up | [Mach64 16-bpp scanout is wrong](issues/2026-08-16-tier0-defects-deferred.md); no physical ATI validation or native 2D backend |
| Generic VESA | QEMU/86Box plus [physical GMA 950](issues/2026-08-27-netbook-gma950-findings.md) and physical Trio3D | Dynamic BIOS modes, software drawing; Have-Disk permits unlisted cards, whose behavior still needs measurement |
| Matrox Millennium II | Historical physical and 86Box software-GDI passes with the stock Matrox mini-VDD | [Guarded mixed-pair boundary](specifications/matrox-millennium2-bringup.md); no claim that the current archive or replacement mini-VDD is physically validated |
| Matrox Millennium (MGA-2064W) | The BIOS advertises 20 linear-framebuffer modes at its BAR1 base, the kit has set `0117h` and restored it, the aperture round-trips 32-bit accesses once CRTCEXT3 bit 7 is set, and this card measures 8 MiB. A guarded candidate carries the chip, with a nine-mode table behind its forced single mode | [The candidate](decisions/2026-09-10-the-2064w-is-drivable-by-the-vbe-path.md) and [the aperture and memory](decisions/2026-09-11-the-2064w-aperture-opens-with-mgamode.md); Windows has still set no mode and drawn no pixel, `VideoMemoryBytes` stays at the 2 MiB floor because 8 MiB is this card rather than the part, and CRTCEXT3 bit 7 is deliberately not written - the one run that settles whether it needs to be is a read of that register with the card in `0117h` |
| Unsupported cards with measurements only | BringupKit baselines on the Debian host `bringup-target`: [Riva TNT and TNT2 Model 64](decisions/2026-09-11-nvidia-nv4-and-nv5-baselines.md), [Cirrus GD5430](decisions/2026-09-11-cirrus-gd5430-memory-and-mode-table.md) | No code carries any of these chips. The records exist so a future family starts from measurements: both NVIDIA parts leave `CR11` bit 7 set so `CR00`-`CR06` writes are discarded, their BAR sizes are lower bounds rather than memory sizes, and the GD5430's size is decodable from `SR0F` while its aperture is closed until `SR07` opens it |

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
| Measure the texture-format fix on the hardware path | 0.7.1 changed `v9x_d3d_texture_format` on the same argument that fixed the software engine, and shipped it on a default path without running it. Run Final Reality's Robots scene on the 86Box ViRGE/DX guest and then on the physical ViRGE/DX, with the refusal counters read before and after | A `V9XTRACE` pair showing `D3dTextureRefusedFormat` and a textured frame, added to [the record](decisions/2026-09-11-a-texture-with-no-pixel-format-is-in-the-displays-format.md), which currently says only that the change was read from the source |
| Explain the `DisplayFmtTex*` disagreement | The probe cell draws nothing where the application draws correctly, so it carries no verdict and the probe is silent about the case every real application hits. The untested candidate is a handle recycled from the cell that runs before it: move the cell earlier and re-run | The cell either gains a pass/fail key or the record names what makes it differ from Final Reality |
| Close Trio64 shield/text validation | On BARRY, start with text off and the shipping shield installed; run the bounded text probe, DOS-box cycles and acceleration harness before deciding the text default | [Text issue](issues/2026-09-06-text-acceleration-hangs-physical-trio64.md) and [DOS-box issue](issues/2026-09-06-dos-box-doubles-the-desktop-on-physical-trio64.md) updated with the exact binaries and results |
| Make desktop/DOS transitions dependable | Finish the remaining physical fullscreen-DOS return and live-mode repaint investigations; keep scanout checks independent of GDI readback | [Fullscreen DOS](issues/2026-08-28-fullscreen-dos-scanout.md), [BARRY repaint](issues/2026-08-20-live-mode-switch-no-repaint-barry.md), [VBE scanout](issues/2026-08-27-vbe-1024x768x16-scanout-stripes.md) |
| Close Direct3D compatibility holes | The target switch, the primary-chain blend and the suspected texture loss are all closed as probe faults - an uncleared depth buffer and handles cached across a switch - and the chain rung now draws correctly end to end. Final Reality's untextured scenes are closed in the software engine by the 0.7.1 format fix. What remains is Incoming's requested texture format, the Trio3D blend state, two devices at once, and 3DMark 99's black boxes on silicon | [Refutations](decisions/2026-09-10-the-render-target-switch-and-the-uncleared-depth-buffer.md), [Incoming](issues/2026-09-05-incoming-refuses-the-hal-texture-formats.md), [latest blend investigation](decisions/2026-09-05-a-register-capture-of-both-trio3d-blend-states.md), [two devices](issues/2026-09-04-a-second-d3d-device-on-the-primary-chain-kills-the-caller.md) |
| Make software Direct3D practical | Edge stepping, the 86Box RAM/aperture benchmark, scalar fixes 2 to 6 and the `D3DSoftSysMem` timing are done; the option measured zero on Final Reality and the second guest showed the aperture, not the CPU, is the bound. Next: paired stores and Z residency, both of which want a physical aperture number, then a named-game gate | [Edge stepping](decisions/2026-09-07-software-rasterizer-edge-stepping.md), [scalar fixes](decisions/2026-09-10-rasterizer-scalar-fixes.md), [sampler fixes](decisions/2026-09-10-rasterizer-texel-units-and-bilinear.md), [the measured zero](decisions/2026-09-11-d3dsoftsysmem-buys-final-reality-nothing.md), [plan](plans/software-rasterizer-scalar-fixes.md); every gain is about half as large on the VRAM target, and physical timings are still needed before residency policy, hybrid assistance or [SMP workers](plans/software-d3d-smp-workers.md) |
| Add Voodoo3 as the next native family | The emulator side is further along than the plan's phase order suggests: `Win98SE-BX-Voodoo3` boots on port 9874 and reports `121A:0005`, and a Voodoo3 3500 already runs through the `vbe` package on the fast guest. What is outstanding is Phase 0 - the physical survey, which also decides whether the emulated model is the right placeholder - then tier-0, 2D and D3D with a named-game gate | [Voodoo3 plan](plans/3dfx-voodoo3-family.md) and [the two guests](vm-environment.md); the shared D3D core/engine split is already complete |

MTRR write-combining remains inspect-only; writing registers needs the physical
evidence specified in its [Stage A record](decisions/2026-08-28-mtrr-stage-a-inspect-only.md).
Hardware cursor work remains proposed. Synthetic vblank is not a current
tier-0 gap: the HAL already reads the VGA vblank status bit. See the corrected
[tier-0 plan](plans/tier0-quality.md).

The A8U4I5 framebuffer read-after-write hard lock reproduced under Microsoft's
driver too; it is tracked as a [board/card investigation](issues/2026-09-06-a8u4i5-trio64-hard-locks-on-framebuffer-readback.md),
not evidence that the shield or default acceleration causes that failure.
