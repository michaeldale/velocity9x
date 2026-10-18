# Gen3 DirectDraw blits at 32 bpp: on the blitter, pixel-exact

2026-10-18, MICHAEL-NETBOOK (945GSE / GMA 950), Windows 98 SE, 1024x576
panel. Phase 1 of
[`../plans/32bpp-acceleration-and-3d.md`](../plans/32bpp-acceleration-and-3d.md).
Evidence: [`../probe/netbook-gen3-32bpp-blits-2026-10-18/`](../probe/netbook-gen3-32bpp-blits-2026-10-18/).

## The change

`i9xx_blt.c` built XY_COLOR_BLT and XY_SRC_COPY_BLT for 1 and 2 bytes a
pixel and refused 4, so a 32 bpp desktop's fills and copies ran on the
CPU through the uncached aperture. At 4 bytes the builder now writes BR13
depth code 3 and sets the command's write-enable bits 21:20 (alpha and
RGB), and the fill colour keeps the whole DWORD. Both are what
`V9X_I9XX_XY_COLOR_BLT` already carries for the Phase 4 breadcrumb, which
the netbook has executed after every batch since 2026-09-14, so the 32 bpp
fill form was already in use on this chip. The copy form with the write
bits (`54F00006h`) had not been run before this record.

The decoder accepts 4 bytes with the same bounds. A 32 bpp fill has the
breadcrumb's command word, so the decoder tells them apart by the
breadcrumb's address: a packet aimed there must have the breadcrumb's
exact shape. At 8 and 16 bpp the write bits are still refused. Host tests
were written first and failed (15 checks) before the builder changed.

## Measured

`V9XDDF` at the desktop's depth (`V9XMSW /set:1024x576xN`): a whole-surface
fill, a rectangle fill and a non-overlapping copy on a 64x16 off-screen
surface, every pixel compared against a CPU reference; then a `V9XTRACE`
snapshot.

| Boot | Driver | Desktop | Fill | Rect fill + copy | Blits on the engine | Breadcrumbs, timeouts |
|---|---|---|---|---|---|---|
| 207 | 0.19.0 (installed) | 32 bpp | `0012A5C3`, 0 mismatches | `00ED5A3C`, 0 | none (no `CountBltEngine`) | 0, 0 |
| 208 | 63e09ecc + this change | 32 bpp | `0012A5C3`, 0 | `00ED5A3C`, 0 | 3 of 3 | 3, 0 |
| 208 | same | 16 bpp | `07E0`, 0 | `F81F`, 0 | 6 of 6 (cumulative) | 6, 0 |
| 208 | same | 8 bpp | `5A`, 0 | `A5`, 0 | 9 of 9 (cumulative) | 9, 0 |

`EngineIdleTimeouts=0` and `RingSpaceTimeouts=0` throughout. The new set
went in as DRV, VxD, HAL, SETP and GL together by WININIT rename, then
`reset`; the 0.19.0 files are kept in that session's scratchpad.

## Findings

- **The 945's blitter fills and copies at 32 bpp with BR13 depth 3 and the
  write bits set**, every byte of the test colours intact. The colours
  differ in every byte, so a wrong byte mask or a 16-bit depth code would
  have shown.
- 8 and 16 bpp are unchanged on the same boot.

## Not measured

- Throughput. Neither run times anything, and per the plan nothing will be
  compared against 16 bpp.
- An overlapping copy at 32 bpp: it is refused by the builder as at every
  depth, and the CPU serves it.
- A full-screen present at 32 bpp from a title. Half-Life sets its own
  640x480x16 mode, and V9XDDP likewise.
- `BuildsAgree=0` in the boot 208 snapshots is the guest's 0.19.0
  `V9XTRACE.EXE`; the DRV and HAL builds match.
