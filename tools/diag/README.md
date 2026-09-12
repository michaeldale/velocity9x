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

Removed on 2026-09-12, recoverable from git history: `dos_box_test_win32.c`,
`io_trace_win32.c`, `matrox_mmio_query.asm` and their six build scripts. No
document referenced them.
