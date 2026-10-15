# Current status and next work

Reviewed against the checkout and the recorded evidence on 2026-10-15.
Update this page when a default, a validation result or the next step
changes; the dated decision records and issues are the history.
[PLAN.md](../PLAN.md) is the original planning baseline, not the current
backlog.

## Release and checkout

The latest release is [0.18.4](../releases/0.18.4/README.md), built from
`ae1c546f`, published on GitHub and mirrored to the update server
(stable). Its archives are host-audited. The last update taken through
the updater on a guest was 0.18.1 to 0.18.2 on the 86Box VT2, committed
at boot 125 with all eight files equal to the package; 0.18.3 and 0.18.4
have not been. Neither has had an activation smoke test from a Have Disk
install of the archive itself. Field confirmation: GitHub issue 6's
reporter runs 0.18.3 and issue 7's runs 0.18.4. The checkout is 0.18.5,
unreleased.

The 0.18.0 release's two owed checks are done, both on the 86Box Mach64
VT2 on 2026-10-15:
- **Have Disk** of the released 0.18.0: every file is the release's by
  hash, and ATI's own ATI3DCIF.DLL was replaced without a prompt
  ([probe](probe/86box-mach64vt2-have-disk-0.18.0-2026-10-15/README.md)).
- **Check for updates** from it to 0.18.1: committed after one restart,
  all eight installed files equal the 0.18.1 package by hash, and the
  running driver reports `2cbf6777`
  ([probe](probe/86box-mach64vt2-update-0.18.0-to-0.18.1-2026-10-15/README.md)).

## Target coverage

One physical machine per card unless the row says otherwise. A8U4I5 is
the Pentium III test box whose card is swapped. "The netbook" is an HP
Mini 110 (Atom N280, 945GSE).

| Target | Recorded coverage | Boundary |
|---|---|---|
| Intel GMA 950 (`8086:27AE`) | The netbook: Direct3D on Gen3 with asynchronous submission and ring flips, DirectDraw on the blitter, OpenGL with a CPU fallback for refused draws, Glide 2 and 3 (NFS II SE, Carmageddon II, Rollcage), DirectX 8 through DDI 6, and 3DCIF's SDK demos ([GMA 950 3DCIF](probe/netbook-gma950-3dcif-2026-10-14/README.md)) | Modes are the video BIOS's: 640x480 and 1024x576 on the netbook. GDI is drawn by the CPU. No emulator runs the engine. A reporter's embedded board on an external VGA monitor hard-locked with the flip on; fixed in 0.18.3 and confirmed ([issue 6](issues/2026-10-14-github-6-gma950-embedded-hard-lock-on-3d.md)) |
| Intel GMA 900 (`8086:2592`, 915GM) | Claimed from a user's survey with the GMA 950's engine ([record](decisions/2026-10-08-intel-915gm-gma900-support.md)); a reporter runs Direct3D on it under 0.18.4 | Not run here. A reporter sees Carmageddon II exit after its intro ([issue](issues/2026-10-10-carmageddon2-exits-after-intro-gma900.md)). The 3DMark 2000 pattern was the polygon stipple, fixed in 0.18.4 and confirmed ([issue](issues/2026-10-11-3dmark2000-pattern-corruption-gma900.md)) |
| ATI Rage XL PCI (Rage Pro class) | A8U4I5: Direct3D on the Mach64 engine, page flips, OpenGL with `GL_SGIS_multitexture`, DirectX 8, Glide 2 and 3, 3DCIF's SDK demos, and the 5:5:5 desktop by default ([555](decisions/2026-10-14-rage-xl-555-desktop.md), [3DCIF](probe/a8u4i5-rage-xl-3dcif-2026-10-14/README.md)) | No CPU fallback: a refused draw is not drawn. 3DCIF's texel-times-vertex-alpha sprites are refused ([issue](issues/2026-10-14-3dcif-blended-sprites-refused.md)). GDI is drawn by the CPU |
| ATI Rage Mobility-M (`4C4D`) | One Gateway Solo 2150: Direct3D and OpenGL on the Mach64 engine (0.9.1, [plan](plans/ati-rage-mobility-hardware-3d.md)) | Not run since the Rage Pro class changed: the 5:5:5 desktop reaches it by design and has not booted on it. Intermittent hard lock under the broad probe ([issue](issues/2026-09-30-gateway-intermittent-black-screen-probe-lock.md)) |
| ATI Rage IIC (`4757`) | A8U4I5: Direct3D on the Rage II trapezoid engine with CPU setup, page flips, OpenGL, Glide 2 and 3, 3DCIF's SDK demos, and the 5:5:5 desktop by default ([desktop](decisions/2026-10-14-rage-iic-555-desktop.md), [Glide](decisions/2026-10-14-rage-iic-555-textures-and-glide.md), [plan](plans/ati-rage-iic-hardware-3d.md)) | Texel times vertex alpha is refused ([issue](issues/2026-10-03-rage-iic-texel-times-vertex-alpha-refused.md)). The blend reads back one step low at 5:5:5 ([issue](issues/2026-10-14-rage-iic-blend-one-step-low-at-555.md)) |
| ATI Mach64 VT2 (`5654`) | Emulator tier-0 | No physical validation. The [16-bpp scanout defect](issues/2026-08-16-tier0-defects-deferred.md) was recorded on the emulator |
| S3 ViRGE/DX | Extensive 86Box regression, including Direct3D and OpenGL on the S3D. On silicon: GDI, text and DOS-box checks ([record](decisions/2026-09-06-advfunc-shield-ships.md)) | Its 3D is measured on the emulator, not on silicon |
| S3 Trio3D/2X and Trio3D (`8A13`, `8904`) | Physical hardware Direct3D: Final Reality and 3DMark 99 ([0.7.0](../CHANGELOG.md)); the Trio3D on the ViRGE path ([record](decisions/2026-10-04-trio3d-8904-on-the-virge-path.md)) | Half-Life refuses Direct3D and OpenGL on the 2 MiB Trio3D ([issue](issues/2026-10-04-trio3d-half-life-refuses-d3d-and-gl.md)) |
| S3 Trio64 / Trio32 | Two physical machines: PCI GDI acceleration ([record](decisions/2026-08-27-crystalmark-barry-accelerated.md)), and VLB under Windows 95 ([handover](handoffs/2026-08-22-vlb-manual-select-handover.md)); a Trio32 guest | VLB Win95 runs without the mini-VDD. Text acceleration stays off: it hung physical Trio64 ([issue](issues/2026-09-06-text-acceleration-hangs-physical-trio64.md)) |
| Matrox Millennium II (MGA-2164W) | A8U4I5: Direct3D on the MGA 3D engine, measured register by register; GDI and DirectDraw on the engine; all 8 MiB walked ([D3D](decisions/2026-10-10-mga2164w-d3d-engine.md), [plan](plans/matrox-mga2164w-hardware-3d.md)) | The chip cannot blend: Half-Life's sprites are keyed and its lighting stippled. Fog, specular, 4444 and filtering are refused. The settings page shows 4 MB ([issue](issues/2026-10-11-settings-page-shows-4mb-on-8mb-millennium-ii.md)) |
| Matrox Millennium (MGA-2064W) | A8U4I5 and an 86Box guest: modes to 1600x1200 at 8, 16 and 32 bpp; GDI fills, copies and text, DirectDraw and page flips on the engine ([plan](plans/matrox-millennium-family.md)) | No 3D engine: Direct3D is the CPU rasterizer. The captured desktop looks soft under both drivers ([issue](issues/2026-10-09-mga2064w-capture-looks-soft.md)) |
| SiS 6326 (`1039:6326`) | A8U4I5: DirectDraw and page flips on the 2D engine; Direct3D and OpenGL on the 3D engine, with no check failing that SiS's driver passes ([plan](plans/sis-6326-hardware-3d.md)) | GDI is drawn by the CPU. One of two cards hard-locked the machine and is parked |
| 3dfx Voodoo4 4500 (VSA-100) | A8U4I5, PCI and AGP: DirectDraw and GDI fill and copy on the 2D engine; Direct3D and OpenGL on the 3D engine; 3dfx's own Glide runs and exits cleanly ([Glide](decisions/2026-10-12-glide-games-run-on-3dfx-glide-and-exit-cleanly.md), [OpenGL](decisions/2026-10-13-voodoo4-opengl-and-subpixel-correction.md)) | Fog, specular, trilinear, 32-bit colour, the second TMU and hardware flips are not used. A Voodoo5 binds and is untested |
| Generic VESA (`vbe`) | QEMU and 86Box; tier-0 on a physical GMA 950 and Trio3D | Software drawing. Have Disk permits unlisted cards, whose behaviour is unmeasured |

## Interfaces

| Interface | State | Limit |
|---|---|---|
| Direct3D HAL (DirectX 5 to 7) | Every card above with a 3D engine; the CPU rasterizer elsewhere (default in `vbe` at 16 bpp, opt-in otherwise) | Only what each engine draws exactly; a refused draw falls back only on Gen3 |
| DirectX 8 (DDI 6) | Rage XL and GMA 950: DxDiag and 3DMark 2001 SE to a score | 16-bit colour, depth and textures only; no stencil; no Direct3D 9 |
| OpenGL 1.1 ICD | Every 3D engine and the CPU rasterizer | Lighting, display lists, evaluators, feedback and selection not implemented ([inventory](plans/opengl-1.1-requirements.md)). Runs at roughly half Direct3D's speed |
| Glide 2 and 3 | Every package but `3dfx`; measured on the GMA 950, Rage XL and Rage IIC | Not measured on the S3, Matrox or SiS cards |
| ATI 3DCIF | Every package; ATI's eight SDK demos on the Rage IIC, Rage XL and GMA 950 ([plan](plans/ati-3dcif-wrapper.md)) | No game run yet. No mipmaps, tiled textures, meshes or TLVERTEX ([issue](issues/2026-10-14-3dcif-unimplemented-features.md)) |
| 16-bit colour layout | 5:5:5 by default on the ViRGE and on ATI with hardware Direct3D; 5:6:5 elsewhere; the settings page overrides | The GMA 950's video BIOS has no 5:5:5 modes |
| Updates and reports | Check for updates and Send report on the settings page, since 0.15.0. One update taken end to end, 0.18.0 to 0.18.1 on the 86Box Mach64 VT2 ([probe](probe/86box-mach64vt2-update-0.18.0-to-0.18.1-2026-10-15/README.md)) | Not run on physical hardware; rollback from the backup not exercised |

No card has a hardware cursor. GDI is accelerated on the S3, Matrox and
Voodoo4 cards and drawn by the CPU on the rest.

## Open work, in order

Priority is a judgement; [issues/README.md](issues/README.md) lists every
open record.

| Priority | Next step | Acceptance evidence |
|---|---|---|
| A 3DCIF game | The SDK demos run. A tester ran Formula 1 and Tomb Raider on 0.18.0 and offers logs; both need more work. The same tester's Cubes cleared blue, which says that desktop was 5:6:5; the cause is unknown until `V9XCIF.LOG` arrives. Tomb Raider's ATI patch needs a sound device on our test machine; Croc needs no patch | A census and a decision record for one game on the Rage XL |
| 3DCIF follow-ups, parked | The blended-sprite decision ([issue](issues/2026-10-14-3dcif-blended-sprites-refused.md)), the Phase 3 readback probe ([issue](issues/2026-10-14-3dcif-phase3-probe-not-written.md)), Gen3 flicker and speed ([issue](issues/2026-10-14-3dcif-gen3-flicker-and-speed.md)), Gen3's 1024x256 refusal ([issue](issues/2026-10-14-gen3-refuses-1024x256-texture.md)) | Each issue's own next step |
| The Rage IIC blend at 5:5:5, parked | A SCALE_3D_CNTL low-nibble sweep in the ATIRX scene tool | The [issue](issues/2026-10-14-rage-iic-blend-one-step-low-at-555.md) names the bit, or rules the nibble out |
| The GMA 950 draws a colour-keyed texture's key colour | Found by the ColorKeyHalves check; reproduces on the netbook ([issue](issues/2026-10-15-gen3-draws-the-colour-key.md)) | The check passes on the netbook |
| The GMA 900 field report | Carmageddon II's exit on a reporter's 915GM, to be re-run on 0.18.4 | The issue updated from the reporter's next files |
| HAL statics across processes | The HAL's static data is shared by every DirectDraw process ([issue](issues/2026-10-06-hal-statics-shared-across-processes.md)) | A design for per-process state, or a record that the sharing is safe |
| S3 physical validation | The ViRGE/DX's 3D on silicon; text acceleration and the DOS-box shield on the physical Trio64 | A physical run recorded against the [text issue](issues/2026-09-06-text-acceleration-hangs-physical-trio64.md) and the [DOS-box issue](issues/2026-09-06-dos-box-doubles-the-desktop-on-physical-trio64.md) |
