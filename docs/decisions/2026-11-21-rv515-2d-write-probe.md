# RV515 2D write probe: the RS690 builder is byte-exact on the X1300

Date: 2026-11-21
Machine: remote-help `M-5CAPM3`, Radeon X1300 `1002:7146`, boot 36, on the
ati tier-0 row (`e25a096f`) at 640x480.
Probe: `R52D.VXD` + `V9XR52D.EXE` (`tools\diag\rv515_2d*`,
`scripts\build-rv515-2d.ps1`), a copy of the RS690's `V9XRS2D` with the
RV515's id and BAR0 alignment. The builder under test is the RS690's
(`src\chipsets\ati\rs690\rs690_2d.c`), unchanged. Reports in
[`docs/probe/m-5capm3-rv515-2026-11-21/2d-write-probe/`](../probe/m-5capm3-rv515-2026-11-21/2d-write-probe/).
Plan: [`ati-rv515-x1300.md`](../plans/ati-rv515-x1300.md), phase 3.

## Result

| Desktop | Fill | FillUnaligned | CopyForward | CopyRight | CopyDown | CopyDownRight |
|---|---|---|---|---|---|---|
| 8 bpp | PASS | PASS | PASS | PASS | PASS | PASS |
| 16 bpp | PASS | PASS | PASS | PASS | PASS | PASS |
| 32 bpp | PASS | PASS | PASS | PASS | PASS | PASS |

Every test: 0 mismatched bytes against the C model, on a 64-pixel-wide
4 KiB surface at frame-buffer offset 16 MiB. The depths were set live with
`V9XMSW /set:640x480xN`, and the desktop went back to 640x480x16 at the end.
`BootCounter` stayed 36 throughout.

## State read on the way

- VxD status 17h: found, both BARs mapped, identity held
  (`CONFIG_MEMSIZE` 256 MiB). BAR0 `D0000000`, BAR2 `FDEF0000`: the
  probe's own BAR read takes the 64 KiB-aligned register window.
- `CP_CSQ_CNTL` 0, so the probe was allowed to touch the engine.
- `HDP_FB_LOCATION` gives the frame buffer at GPU `D0000000`, as the
  register probe read.
- `DP_DATATYPE` read `00010F06` before the init list at all three depths.
  The BIOS leaves it the same whatever the VBE mode; the builder's init
  list sets the depth itself.
- `RBBM_STATUS` `10000140` at start and end: idle, 64 free FIFO entries.
- Waits: two status reads before each op and four or five after,
  destination-cache flush included. The engine finishes these small ops
  almost at once.

## What it settles

The RS690's 2D builder values (init list, fill, forward and reversed
copies) apply to the RV515 unchanged, as the plan's table expected. The
next step is the engine32 side: a `fill_engine_descriptor` hook on the
RV515 row that reads BAR2 with `V9xPciReadAtiMmioBar` and maps it, then
DirectDraw fill and copy on that engine. Not done here.

Not covered: large fills and copies, the destination cache under
sustained load, and 2D after a DOS box or mode change mid-session.
