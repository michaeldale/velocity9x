# The Voodoo4's GDI fills and copies run on its 2D engine, PASS at 8 and 16 bpp

Date: 2026-10-11. Machine: A8U4I5, boots 412-415. Card: retail Voodoo4 4500
AGP. Build: branch `vsa100-tier0` on `b323939` plus this commit's
`gdi_accel.c` change. Evidence:
[gdi-boot412](../probe/a8u4i5-voodoo4-2d-2026-10-11/gdi-boot412/),
[gdi-boot413](../probe/a8u4i5-voodoo4-2d-2026-10-11/gdi-boot413/).
Plan: [3dfx-vsa100-family.md](../plans/3dfx-vsa100-family.md), Phase 3.

## What was wired

`gdi_accel.c` gains a TDFX arm, compiled into the 3dfx family's DRV only
(`V9X_TDFX_FAMILY`), on the pattern of the MGA one: idle as three consecutive
clear status reads, the engine selector over the 2D block the HAL already
uses, fill and copy built at gate 8 by `tdfx_2d.c` (now linked into the DRV
too), issued after an idle wait. Uploads and text decline; text was already
masked off for any engine other than the S3s and the MGA.

## Measured with V9XGDI /accel

500 seeded operations, 20 comparisons against a reference DC, an injected
timeout at the end; one run per boot because the poison latches.

| Boot | Mode | Fills / copies on the engine | DeclineEngine | Compared | Injection | Result |
|---|---|---|---|---|---|---|
| 412 | 640x480x16 | 74 / 132 | 126 | PASS | not consumed | FAIL |
| 413 | 640x480x16 | 200 / 132 | 0 | PASS | poisoned | PASS |
| 414 | 640x480x8 | 165 / 131 | 0 | PASS | poisoned | PASS |
| 415 | 1024x768x16 | 166 / 131 | 0 | PASS | poisoned | PASS |

`IdleTimeouts=0` in every run. Boot 413 also put all 48 clipped operations on
the engine.

## The boot 412 failure, and what fixed it

126 operations were accepted by the gates and refused by the builder. A
solid brush's `Bits` carry the pixel replicated across the dword; the MGA
builder masks the colour to the pixel, the VSA-100 builder refuses a colour
wider than the pixel. The arm now masks `op.color` with the depth's all-ones
value before building. The harness's injected timeout landed on one of the
refused operations, which never reached the idle wait, hence "injection not
consumed". Boot 413 onward: no refusals, injection consumed.

## Not established

- 32 bpp (not accepted by the arm; nothing measured there).
- Speed: nothing timed; the S3s' 1024-pixel threshold is kept.
- What the monitor shows: every check reads back through GDI.
- Other families: the change is behind `V9X_TDFX_FAMILY`, and no other
  family's `/accel` was rerun.
