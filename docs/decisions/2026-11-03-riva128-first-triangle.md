# RIVA 128ZX: the first Direct3D triangle, and what drawing it settled

Date: 2026-11-03

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition, boot 18, desktop 800x600x16. Plan: `docs/plans/nvidia-riva128-d3d.md`,
step 1. Files: `docs/probe/m7v636c-riva128-d3d-2026-11-03/run1-7`.

## The probe

`V9XN32D /d3d` (tools\diag\nv3_2d_win32.c and its VxD, nv3_2d.asm). It runs
the Phase 3 bring-up, adds rivafb's D3D pieces, which the 2D runs leave out,
and draws one right triangle, (8,2) (72,2) (8,14), on a 16 bpp X1R5G5B5
surface in the scratch window 2 MiB into video memory. Nothing is on
screen, so the 5:6:5 desktop does not matter. It then reads the surface
back. The VxD now also accepts methods 1000h-1FFCh, the D3D vertex array,
and reads PGRAPH's trap and DMA registers.

What it writes, from rivafb's tables (riva_tbl.h), decoded with
nv3_pgraph.xml:

- RAMHT 930h: handle 80000013h, context 00D70347h (type 17h, the textured
  triangle; instance 347h). Subchannel 7, as riva_hw.c's Tri03 at USER +
  E000h.
- The instance at RAMIN 3470h: 10419208h (X1R5G5B5, zeta enabled,
  destination surface mask 4, so it draws into **surface 2** with zeta in
  **surface 3**), then 140h (the DMA object at RAMIN 1400h).
- The DMA object: flags 00010000h, limit, page entries `(page << 12) | 3`.
  86Box's vid_nv3.h names the flag bits: 16 is page table present and
  24-25 the target (0 video memory, 2 PCI, 3 AGP). rivafb maps 8 MiB with
  800h entries, which run over RAMRO (2000h) and RAMFC (2200h); the probe
  maps only the window's 16 pages.

The draw trigger is envytools' (nvhw/pgraph.c `pgraph_prep_draw`, class
17h, read for this). When its V coordinate arrives, a vertex is stored in
the slot named by its specular word's low nibble. Then each of the nibble
triples 0-2 and 3-5 that names three distinct slots draws that triangle.
The draw also needs the texture offset, format, filter, config and fog
colour all set. The probe sends v0 with slot 0 repeated, v1 with slot 1
repeated, and v2 with slots 2, 0, 1. Coordinates are IEEE floats.

## Seven runs, and the hypotheses they killed

| Run | Change | Result |
|---|---|---|
| 1 | rivafb's state, blend with source alpha, over the pattern | Every pixel inside the triangle was written back as the destination with bit 15 cleared (`E618` became `6618`): rasterised, source factor 0 |
| 2 | surface cleared to 0; ROP ADD; DST_BLEND ZERO | All 0: the **source colour** is black, not only transparent |
| 3 | fog byte 255 with a white fog colour; fog byte 0 | 255: solid white (`7FFF`). 0: still black |
| 4 | DMA target 1, 2, 3; flags 0; the framebuffer's bus address in the page entries | Targets 1-3: identical varying reds, texels from **system RAM** at the entries' addresses. Target 0 and the bus address: black. Flags 0: timed out, machine fine |
| 5 | targets 1 and 2 at the bus address | Black |
| 6 | PGRAPH DMA state read back | Video memory target, entry `0020C003`, offset `C000`, no DMA interrupt: the translation was right |
| 7 | texture moved from C000h to F000h | **Pass**, below |

Runs 1-6 had a bug of the probe's own. The zeta surface (window 8000h, 16
lines of 1600 bytes, to E400h) covered the texture at C000h, and clearing
zeta zeroed the texture. Every video memory texel therefore read 0, which
looked like a DMA problem. What runs 3 and 4 did show stands: the fog
byte's meaning, and that targets 1-3 read system memory.

Run 7, with rivafb's DMA object (video memory target, page table present):

| Variant | Pixels inside / outside the ideal triangle | Values |
|---|---|---|
| White texture, red vertex colour, fog 0 | 384 / 36 | all `7C00` |
| Red/blue 4x4-block checker, white vertex colour | 384 / 36 | `7C00` x212, `001F` x208 |
| Fog byte 80h, white fog colour | 384 / 36 | `7E10`, `7E0F`, `7DF0` |

## What this settles

- **The D3D object draws** with rivafb's object, its DMA object, and the
  envytools trigger. No method trapped in any run.
- **The texture modulates the vertex colour**, under CONFIG source colour
  NORMAL (1).
- **The specular word's top byte is the fog amount:** 0 is no fog, FFh full
  fog to the fog colour, 80h half. Direct3D's specular alpha is the
  reverse (255 is no fog), so the HAL sends 255 minus it.
- **The texture reads video memory** through a DMA object whose page
  entries hold video memory offsets.
- **Textures are swizzled**, in Morton order with u in the even bits. A
  linear layout predicts the wrong colour at (30,4) (texel u 2, v 1), and
  the swizzled layout fits all four samples. Four samples are a strong
  hint, not a proof; the next probe uses a texture that tells every texel
  apart.
- **Bit 15 is written 0** at 16 bpp by the D3D object; the 2D objects set
  it.
- **Coverage:** 420 pixels, x 8-71 and y 2-13, against 384 pixel centres
  inside the triangle. The edge rule is not yet worked out.

Not settled: Z (zeta writes were off), perspective, filtering, alpha test
and blend factors other than zero, the edge rule, the full 8 MiB DMA
object (and where in RAMIN it fits without RAMRO and RAMFC), and
rendering into a 5:6:5 surface, which the D3D object cannot do. That last
one is why 16 bpp desktops have to run 5:5:5 for Direct3D (decision B of
`docs/plans/nvidia-riva128.md`).
