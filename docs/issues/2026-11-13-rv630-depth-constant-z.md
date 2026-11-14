# RV630: the depth block writes one constant Z for every fragment

Date: 2026-11-13
Status: **not reproduced after a reset.** Seen only in boot 5; boot 6
draws correct depth with the same binary. Cause in boot 5 unknown; reopen
if the constant comes back.

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

## After a reset (boot 6)

`v9xctl reset` (boot 6), then the same `V9XR63D.EXE` (`89b81545-dirty`,
the binary of boot 5's last sets), captures lettered `r` and `s` in
[`../probe/rv630-3d-2026-11-13/`](../probe/rv630-3d-2026-11-13/):

| Run | Boot 5 | Boot 6 |
|---|---|---|
| `/depth /zfunc:7 /zfill:30003000 /set:28010=10006` (Z32F) | every tile `4994E8C4` | tiles 0-3 `3F400000` (0.75), 4-7 `3E800000` (0.25): the vertices' z |
| `/depth` (Z16, LESS) | nothing drawn | Pixels PASS, DepthBuffer PASS, tiles `8000`/`4000` |
| `/depth ... /zlit:3E800000` | `4994E8C4` | every written tile `3E800000` |
| `/gouraud` | PASS | PASS |

Set `s` wrote CP_QUEUE_THRESHOLDS (8760h = 2B16h) through the ring
before a Gouraud draw, the write that stalled the ME earlier in boot 5.
It did not stall this time, and the depth runs after it stayed correct.
That one run does not support "a threshold stall leaves the DB broken",
and does not rule it out either: the stall itself did not recur.

What boot 5 held that boot 6 does not is not known. Boot 5 had carried
about 70 probe runs, several CP halts and soft resets, and the ME stalls
of the threshold writes before the first depth draw. Watch for the
constant in the Direct3D HAL's first depth runs; if it returns, the
leads above still stand.

The HAL's first runs, boots 7 and 8: V9XD32's depth checks (T3, T5, T8)
pass through `d3d_r6.c`
([record](../decisions/2026-11-13-rv630-direct3d-hal-first-light.md)).
