# 0.39.0 on three real S3 Trio cards

Date: 2026-11-07

Status: measured on three physical cards with the 0.39.0 release
(`186b656c`). Settings reports from the driver's own page; mode and
colour-depth switching and dxdiag were checked by hand. This is not a
`run-release-checks.ps1` run (no V9XD32 / V9XGLP / V9XGLIDP / V9XR3DP).

Evidence (same folder as this record when attached):

- [diagnostics-trio64.txt](diagnostics-trio64.txt)
- [diagnostics-trio64vplus.txt](diagnostics-trio64vplus.txt)
- [diagnostics-trio64v2dx.txt](diagnostics-trio64v2dx.txt)

## Why

The 0.39.0 release-checks record left the S3 package uncovered: no machine
booted its 0.39.0 files
([2026-11-07-0.39.0-release-checks.md](https://github.com/michaeldale/velocity9x/blob/main/docs/decisions/2026-11-07-0.39.0-release-checks.md)).
On [PR #23](https://github.com/michaeldale/velocity9x/pull/23) the merge
note asked for a boot on a real card, especially revision 53h.

## Cards and Have Disk

Have Disk / the Windows driver list behaved as expected. Device Manager
showed the 775, 764x and 765 names on the matching cards.

| Card | Adapter (settings report) | PCI | Revision (report) |
|---|---|---|---|
| Trio64 | S3 Trio32/64 86C764 | 5333:8811 | 00 |
| Trio64V+ | S3 Trio64V+ 86C765 | 5333:8811 | 00 |
| Trio64V2/DX | S3 Trio64V2/DX or /GX 86C775/86C785 | 5333:8901 | 06 |

The Trio64V+ report is revision 00, not 53h.

## Smoke

On each card: mode and colour-depth switching in Windows was OK; dxdiag was
OK.

## Settings reports

| Card | VRAM | MCLK | Stage | GDI | DirectDraw pixel checks | Mode switching |
|---|---|---|---|---|---|---|
| Trio64 | 2 MB | 69.800 MHz | enable-ok | PASS 800x600x16 | 7 of 7 | Live, any depth |
| Trio64V+ | 2 MB | 50.113 MHz | enable-ok | PASS 800x600x16 | 7 of 7 | Live, any depth |
| Trio64V2/DX | 2 MB | 50.113 MHz | enable-ok | PASS 800x600x16 | 7 of 7 | Live, any depth |

All three: linear aperture mapped; Direct3D not advertised
(`SYSTEM.INI Direct3D=0`); clock detector `s3-virge-pll-v1`.

## Not covered

- Revision 53h (Diamond Stealth64 Video 2001 survey) was not this V+ card.
- No automated release probes on these machines.
- No 3DMark.
