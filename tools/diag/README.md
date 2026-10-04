# Diagnostic tools

Guest-side probes and tools, one source per program, built by the matching
`scripts/build-*.ps1`. Most are packaged with the driver and are described in
[docs/specifications/hardware-diagnostics.md](../../docs/specifications/hardware-diagnostics.md)
and [docs/BUILDING.md](../../docs/BUILDING.md).

Four sources are kept without a build script. Each answered one question on
one machine and is retained because the record that cites it needs the source
to be readable; build one with the shared helper in `scripts/lib/diag-tool.ps1`
if the question comes back.

| Source | Record it served |
|---|---|
| `matrox_inventory_win32.c` | [2026-08-27 netbook GMA950 findings](../../docs/issues/2026-08-27-netbook-gma950-findings.md) |
| `matrox_mmio_query_win32.c` | same |
| `surface_step_win32.c` | same |
| `trio_ctx_probe.c` | [2026-08-27 GDI accel corrupts display on physical Trio64](../../docs/issues/2026-08-27-gdi-accel-corrupts-display-on-physical-trio64.md) |

`ati_mmio_fingerprint.asm` and `ati_mmio_fingerprint_win32.c` form the
standalone Phase 0 Rage Mobility-M fingerprint. Build them with
`scripts/build-ati-mmio-fingerprint.ps1`; run `ATIMM.EXE` beside `ATIMM.VXD`.
It publishes `C:\V9XDIAG\ATIMM.TXT` and performs no engine or PCI writes. The
LCD index selector is the sole MMIO write and is restored around each indexed
panel read.

`ati_rage_iic_probe.asm` and `ati_rage_iic_probe_win32.c` are the Rage IIC
(`1002:4757`) equivalent, with no MMIO write at all. Build them with
`scripts/build-ati-rage-iic-probe.ps1`; run `ATIIC.EXE` beside `ATIIC.VXD`,
optionally with `/block1`. It publishes `C:\V9XDIAG\ATIIC.TXT` and the 64 KiB
shadow at C0000h as `C:\V9XDIAG\ATIIC.ROM`. See
[2026-10-02 Rage IIC register survey](../../docs/decisions/2026-10-02-rage-iic-register-survey.md).

`sis6326_probe.asm` and `sis6326_probe_win32.c` are the SiS 6326
(`1039:6326`) Phase 0 survey. Build them with `scripts/build-sis6326-probe.ps1`;
run `SIS6326.EXE` beside `SIS6326.VXD` under SiS's own driver, optionally with
`/force3d` to read the 3D block when SR39 D2 is clear. It publishes
`C:\V9XDIAG\SIS6326.TXT` and the C0000h shadow as `C:\V9XDIAG\SIS6326.ROM`.
It writes no MMIO or PCI register; the sequencer and CRTC index ports are
written to read SR00-SR3F, CR00-CR3F and CR80, and restored. With the
extensions locked (SR05 = 21h) every SR06+ read returns 21h, so the report
suppresses its decodes and reads no MMIO. `/unlock` makes the one data write:
SR05 = 86h, read, then SR05 = 00h to lock again. See
[the register reference](../../docs/specifications/sis6326-registers.md).

`sis6326_2d.asm` and `sis6326_2d_win32.c` are the SiS 6326 2D engine write
probe. Build them with `scripts/build-sis6326-2d.ps1`; run `SIS2D.EXE` beside
`SIS2D.VXD` under Velocity9x tier-0 at 8 or 16 bpp. It **writes the card**:
SR05/SRB/SR27 (read first, written back last) and the engine, into off-screen
VRAM at 2 MiB and above, checking each fill and copy byte for byte against the
host-tested builder it compiles in. It publishes `C:\V9XDIAG\SIS2D.TXT`. See
[2026-10-05 SiS 6326 2D engine writes](../../docs/decisions/2026-10-05-sis6326-2d-engine-writes.md).

`sis6326_3d_win32.c` is the SiS 6326 3D engine write probe. Build it
with `scripts/build-sis6326-3d.ps1` and run `SIS3D.EXE` beside `SIS2D.VXD`
(which it shares with SIS2D) under Velocity9x at 16 bpp; with no switch it
runs phase 1, with `/phase2` the shading, Z16, alpha-test and blend scenes,
with `/phase3` the texture scenes (`/phase3a` the raw pitch-field sweep,
`/phase3m` the mip scenes alone).
It **writes the card**: SR39 (restored) and the 3D registers, firing
triangles into guarded off-screen RGB565 targets (and Z16 buffers) and
comparing each pixel with a reference. It publishes `C:\V9XDIAG\SIS3D.TXT`.
See [2026-10-05 SiS 6326 3D first triangle](../../docs/decisions/2026-10-05-sis6326-3d-first-triangle.md),
[shading and depth](../../docs/decisions/2026-10-05-sis6326-3d-shading-and-depth.md)
and [textures](../../docs/decisions/2026-10-05-sis6326-3d-textures.md).

`ati_rage2_scene.asm` and `ati_rage2_scene_win32.c` run the Rage IIC's
Phase 2 engine scenes ([plan](../../docs/plans/ati-rage-iic-hardware-3d.md)).
Build them with `scripts/build-ati-rage2-scene.ps1`, and run `ATIRX.EXE`
beside `ATIRX.VXD`. The VxD only maps the identity-checked register window
and the aperture. The EXE writes the engine through the host-tested
builders it compiles in, logging each batch to `C:\V9XDIAG\ATIRX.TXT`
before executing it. Unlike the other ATI tools it **writes the engine**,
off-screen at VRAM 2 MiB inside guards.

Removed on 2026-09-12, recoverable from git history: `dos_box_test_win32.c`,
`io_trace_win32.c`, `matrox_mmio_query.asm` and their six build scripts. No
document referenced them.
