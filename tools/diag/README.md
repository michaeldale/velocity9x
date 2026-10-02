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

Removed on 2026-09-12, recoverable from git history: `dos_box_test_win32.c`,
`io_trace_win32.c`, `matrox_mmio_query.asm` and their six build scripts. No
document referenced them.
