# RV630: the depth block writes one constant Z for every fragment

Date: 2026-11-13
Status: **open.** Blocks Direct3D depth on the HD 2600 XT row.

Machine: M-NZHWBR, Radeon HD 2600 XT (`1002:9588`). Tool: `V9XR63D.EXE
/depth` (`tools\diag\rv630_3d_win32.c`). The full list of what was tried,
with captures, is in
[`../decisions/2026-11-13-rv630-gouraud-texture-depth.md`](../decisions/2026-11-13-rv630-gouraud-texture-depth.md).

## Symptom

With a depth buffer bound and Z writes on, every drawn fragment writes the
same Z: `4994E8C4` (1,219,864.5) in DEPTH_32_FLOAT, saturated to FFFFh in
Z16. LESS against a cleared buffer therefore rejects everything. Colour
output is right in every case (ALWAYS draws the expected rectangles).

## Ruled out

Vertex z and w; a z set in the VS before the position export; the
viewport Z transform (window-space and Mesa's ordinary form); the Z clamp;
DB_DEPTH_CLEAR; DB_RENDER_CONTROL/OVERRIDE; Z order and dual export; the
interpolator's position and Z inputs; a PS-exported Z; the export
ELEM_SIZE; every context register that differs from Mesa's R6xx path, and
all of it at once with clipping on; the depth array mode; HTILE base and
surface. No context register (all 1,024 read back) and no PA/SC/SX/SPI/DB
config register holds the constant.

## Leads not yet followed

- The config and memory-controller state Linux's `r600_startup` writes and
  the probe does not: HDP/DCP/DMA tiling copies of GB_TILING_CONFIG,
  ARB_POP's ENABLE_TC128, and anything in `r600_mc_program` / the VRAM
  scratch page the DB might fetch through.
- Config registers outside the ranges read (8A00h-8BFFh, 9000h-905Fh,
  9100h-913Fh, 9800h-98FFh).
- Whether the constant changes across a reboot (a stale on-chip value) or
  with the first depth draw of a boot.
- A known-good R6xx depth command stream (Mesa r600 under Linux, captured)
  to diff packet for packet.
