# Haswell Phase F2b: Y-tiled D16 Z buffers, filled by a masked draw; a zero WM and PS hangs the render engine

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE, boots 11-14.
As designed in `docs\plans\intel-haswell-direct3d.md` Phase F2b. Evidence:
`docs\probe\hsw-4570-depth-f2b-2026-10-17\`.

## Step 1: the /depth16 probe, three runs

`V9XHSWMM /depth16` borrows the driver's idle render ring (START
`01802000`, HEAD = TAIL), runs two batches and gives the ring back
enabled and empty. The fill: a D16_UNORM buffer at pitch 128, 64 rows,
written by a depth-only draw of the whole buffer at z 1.0 and the
sub-rectangle [16,48) at 0.25 under ALWAYS. The scene, under LESSEQUAL:
triangle A at 0.5 red, A again at 0.5 green (equal, so it must pass), C
at 0.75 blue.

| Run | Boot | Fill pipeline | Result |
|---|---|---|---|
| 1 | 11 | WM and PS all zero; SBE one attribute | fill batch never retired |
| 2 | 12 | WM and PS all zero; SBE no attributes | identical hang |
| 3 | 13 | the colour draw's WM, PS and kernel; BLEND_STATE write disables 27:24 all set | pass |

Runs 1 and 2 stopped in the fill batch at ACTHD `01822710`, IPEHR
`7A000003` (a PIPE_CONTROL), INSTDONE `FFDFFFFF`, and the wait timed out.
The probe gave the ring back each time (CtlAfter 1, StartAfter
`01802000`), but RCS stayed wedged and each run cost a power cycle.

- **Killed: SBE's attribute count.** Vol 2b requires SBE's output count
  and PS's Attribute Enable to agree, and run 1 broke that rule. Run 2
  fixed it and hung identically.
- **Not separated: WM against PS.** An all-zero `3DSTATE_WM` (no thread
  dispatch) and an all-zero `3DSTATE_PS` were changed together. Mesa never
  sends a zero PS, even when it disables dispatch. Which of the two the
  engine needs was not measured: a third power cycle would buy no driver
  behaviour.
- **Run 3:** 4032 pixels judged, 0 mismatches, 64 not judged (within
  1/64 pixel of an edge). The equal-z redraw passed (green at 8,8). C
  fell behind the fill's 0.25 square. At (24,24), inside that square,
  target 0 still holds its `A5A5A5A5` guard: the masked fill wrote no
  colour.

`v9x_hsw_draw.depth_only` now means "dispatch the kernel, blend pointer
on `V9X_HSW_SHADE_BLEND_NO_COLOUR`". The host test compares its WM, PS
and SBE with the same draw's colour batch.

## Step 2: the HAL

- **Placement** (`d3d_hsw.c` create_surface): a video-memory
  `DDSCAPS_ZBUFFER` gets pitch = width x 2 rounded up to 128, rows
  rounded up to 32, and a 4 KB base. This uses `v9x_d3d_place_block`, one
  surface in one block; `place_each` gives the same result for one
  surface. V9XDDP's 64x64 Z surfaces reported pitch 128.
- **Fills:** `eng_hsw.c`'s fill sends a Z destination to
  `v9x_d3d_hsw_depth_fill`. That is run 3's batch on the TL path:
  - two triangles on the rectangle's edges at z = value / 65535;
  - DEPTH_STENCIL ALWAYS with write;
  - the colour target is the Z buffer's own memory named as B5G6R5, with
    every channel masked.
- **No fallback:** `V9X_D3D_ENGINE_LIMITS.depth_tiled` (appended) makes
  `v9x_depthfill_body` return `DDERR_GENERIC` rather than CPU-fill a
  tiled buffer.
- **Draws:** a bound Z buffer must be one this file placed (its pitch,
  base and signature) and cover the target. Each draw writes a
  DEPTH_STENCIL_STATE, with D3D's function mapped as the alpha test's is
  and its ZWRITEENABLE. The batch emits the depth buffer after Vol 2b's
  stall, flush and stall.
- **Caps:** ZTEST, all eight Z comparisons, `DDBD_16`.

## Boot 14: V9XDDP

The HAL was replaced by a WININIT rename. V9XDDP was run three ways:
plain, `/zprivate` and `/mixed`.

| Check | Result |
|---|---|
| `D3DZCompareOk`, `D3DZWriteMaskOk` (attached Z, the working device) | 1, 1 |
| `D3DZPCompareOk`, `D3DZPWriteMaskOk` (private device, `D3DZPIsHardware=1`) | 1, 1 |
| `MixedColorEncodingOk` | 1 |
| `MixedOrderingOk`, `MixedDepthEncodingOk` | 0, 0: the Lock limit below |
| everything else in `V9XDD2.INI` | 115 pass; `FlipPixelOk` 0, as on the netbook (boot 178): GDI reads the GDI page |

`MixedClearDepthHr=0` is a depth fill by Blt. With `depth_tiled` set, a
success code can only come from the engine fill, because a declined fill
now fails. The LESS draw at 0.5 after it drew (red at 16,16), so the
cleared depth there was above 0.5.

After the runs, a read-only snapshot shows RCS idle: HEAD = TAIL `858`,
ACTHD at the tail, INSTDONE `FFFFFFFE`.

**Mixed is the Lock limit, as the design predicted.** The probe reads and
writes the Z buffer through a Lock at (16,16) as linear words. In a
Y-tiled D16 buffer of pitch 128, linear byte 2080 is pixel (32,2):

- That pixel lies outside the triangle, so the first read returns the
  clear value 65535 (expected `8000h`).
- The CPU's `4000h` store lands at (32,2).
- So the second draw is accepted at (16,16) instead of rejected (blue,
  `MixedRejectColorRaw=31`).

Detiling on Lock is not done.

## Correction to F2a

V9XDDP rolls its output over into `V9XDD2.INI` when the first file fills
(`ddraw_probe_win32.c:735`). F2a read only `V9XDD.INI`, and the plan
counted 12 failures "for want of Z". In fact 8 of F2a's 22 failures
(`D3DZ*` 2, `D3DZP*` 2, `Mixed*` 4) were the first file's defaults for
checks that are written as 0 at start and run into the second file (the
`D3DZP*` and `Mixed*` ones only under `/zprivate` and `/mixed`). F2a's
other 14 failures (mip levels, trilinear, colour key, DECALALPHA,
chains) are in the first file and stand. Read both files.

## Not done

- No game has run with a Z buffer: none is installed on the 4570 (Phase G).
- A Lock of a Z buffer returns tiled bytes.
- Only the 640x480x16 mode V9XDDP sets was measured.

Gates: check-tree, build-host (the depth-only test failed first, then
passed), run-checks.
