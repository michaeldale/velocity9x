# Rage IIC triangle setup: where Quake 2's frame goes, and halving the CPU part

Date: 2026-10-03. Machine: A8U4I5 (Rage IIC AGP `1002:4757`, P3, Win98SE),
Quake 2 demo attract loop under `V9XGL.DLL`, `+set timedemo 1`.
Evidence: `docs/probe/a8u4i5-rage-iic-registers-2026-10-02/phase6-cost-*`,
`phase6-grid-*`, `phase6-fit-*`, `phase6-px-*` (counter snapshots before and
during each run, `qconsole.log`; the last run holds the cumulative
`V9XGL.LOG`, `phase6-px-q2` the screenshot), and `phase6-setup-bench`
(the host benchmark's source and its output on the P3).

## How it was measured

The Rage II draw now keeps always-on TSC counters for its four parts
(prepare, split decision, piece build, register emit including FIFO waits)
and counts batches, pieces, trapezoids, pixels (each piece's exact area
from its quarter-pixel vertices), register writes and FIFO status reads
(`V9X_D3D_DIAGNOSTICS.r2_cycles`, `r2_work`, ABI 2026100303). Each run is
the difference of two `V9XTRACE` snapshots. The TSC runs at 1.002 GHz
(`ATIRX /fill`, against `GetTickCount`), so the 118.5 G cycles counted
inside the draw over a 148 s run are 80 % of its wall time; the rest is
Quake 2, the ICD's transform and present.

A host benchmark (`bench_r2.c`) drives the same functions over perspective
floor and wall triangles; built as a GUI program (a Win32 console program
on Win98 runs through a DOS VM, which this machine must not do) it also ran
on the P3.

## Results

640x480, bilinear, unless noted. CPU = split + build, per piece, P3 cycles.

| Build | fps | Split | Build | Emit | CPU/piece | Emit/piece |
|---|---|---|---|---|---|---|
| boot 148 (no counters) | 3.2 | - | - | - | - | - |
| closed-form bound, `-ox` | 3.2 | 22.4 G | 34.4 G | 69.2 G | 41,000 | 49,900 |
| + forward-differenced grid | 3.3 | 14.0 G | 35.0 G | 72.2 G | 35,040 | 51,577 |
| + one texture fit per piece | 3.5 | 15.8 G | 17.9 G | 82.7 G | 23,378 | 57,286 |
| + pixel counters (same code) | 3.6 | 10.8 G | 18.9 G | 88.2 G | 19,930 | 59,276 |

320x240 window (`vid_fullscreen 0`, `gl_mode 0`): 5.2 / 5.6 fps with the
bound alone, 5.7 / 6.2 with the grid, 6.5 / 7.0 with the shared fit, 6.8 /
7.1 on the last build.

Last build, per run:

| Run | fps | Pixels/frame | Pixels/piece | Traps/piece | Cycles/pixel | Emit share | FIFO reads/write |
|---|---|---|---|---|---|---|---|
| 640x480 bilinear | 3.6 | 1.06 M | 378 | 1.76 | 211 | 74 % | 1.81 |
| 640x480 `GL_NEAREST` | 4.1 / 4.4 | 1.12 M | 384 | 1.77 | 167 | 68 % | 1.28 |
| 320x240 window | 6.8 / 7.1 | 0.48 M | 99 | 1.55 | 408 | 52 % | 0.59 |

## What changed

- **The split decision's error bound.** The fit is the quadratic
  interpolant of S = U / Q at six nodes. With Q = Qm (1 + e), |e| <= E =
  (q_max - q_min) / (q_max + q_min), S is within R E^2 of a quadratic (R
  half S's range), and quadratic interpolation on a triangle has Lebesgue
  constant 5/3, so the fit's error is at most (8/3) R E^2. Where that is
  under 0.5 texel the sampled grid is skipped. The grid never read above
  0.168 of the bound on any benchmark triangle, and a host test holds the
  bounded decision equal to the grid's on random perspective triangles at
  three limits (it fails with the bound shrunk 100x). Slivers given the
  tangent plane always take the grid.
- **The grid by forward differences.** Along a grid row q, S q and the fit
  advance by constant differences; per point the work fell from ~75
  dependent x87 operations to ~20. Readings agree with the old grid to a
  relative 9e-8 over 4,610 triangles, with no split decision changed.
- **One texture fit per piece**, about the screen origin, moved to each
  trapezoid's anchor analytically, and handed from the split decision to
  the build. The host test's texel misses against the card's model are
  identical in all four sets (68, 74,123, 171,517, 88,043).
- `-ox` for the Rage II files, as the Mach64 per-triangle files have.

## Hypotheses the evidence killed

- **The grid's divides were the cost.** Hoisting them out changed nothing
  on the host (10,400-12,800 cycles per call before and after, run to
  run); the cost was the chain of
  dependent operations, and the change was reverted.
- **Quake 2's geometry is unusual.** The benchmark on the P3 runs 4-6x the
  host's cycles in every function alike (fit + grid 17,400, fit and
  conversion per trapezoid 7,000, two colour setups 2,600); Quake 2's
  pieces cost what that predicts.
- **The CPU limits 640x480.** Cutting the split decision by 8.4 G cycles
  moved the run 3.2 -> 3.3 fps: the saving reappeared as FIFO waiting. The
  engine accepts a trapezoid's registers only once the one before is drawn,
  so its 16-entry FIFO holds under a quarter of a piece, and a piece's last
  trapezoid (~215 pixels, ~40 k cycles) outlasts the next piece's setup
  (~20 k). At 640x480 the engine is the frame; at 320x240 the CPU is half
  of it, and the CPU work moved that run 5.2 -> 6.8 fps.

## The engine's rate: 640x480 runs at the chip's fill rate

Clocks (`ATIRX /pll`, read only, `phase6-clocks/ATIRX-PLL-V9X.TXT`):
PLL_REF_DIV 31, MCLK_FB_DIV 180, PLL_XCLK_CNTL post-divider code 1 (2),
so 2 x 14.318 MHz x 180 / 31 / 2 = 83.1 MHz, the video BIOS's own 8300
entry (clock table at ROM 0x6F0, reference 1432, divider 31). MPLL_CNTL
reads `AD` and MEM_CNTL `10631577`, both as under ATI's driver at boot
125. The 264GT2C has no `MFB_TIMES_4` (atyfb), hence the factor 2.

Per feature (`ATIRX /fill`, `phase6-clocks/ATIRX-FILL.TXT`): a 256x256
quad through the HAL's own builders, 16 times, timed to engine idle.

| Scene | Engine clocks / pixel | Mpixels/s |
|---|---|---|
| flat | 1.5 | 53.6 |
| flat, Z | 3.8 | 22.0 |
| point, 64x64 | 5.8 | 14.2 |
| point, Z | 7.8 | 10.6 |
| bilinear, 64x64 | 12.4 | 6.7 |
| bilinear, 256x256 | 15.4 | 5.4 |
| bilinear, Z | 17.1 | 4.85 |
| bilinear, Z, perspective | 18.0 | 4.6 |
| bilinear, Z, SRCALPHA blend | 19.4 | 4.3 |
| bilinear, Z, Quake 2's lightmap blend | 19.4 | 4.3 |

ATI's driver measured 13.8 MTexels/s point and 7.7 bilinear (3DMark 99)
and Final Reality 6.79 Mpixels/s (native baseline): the same engine
rates. Quake 2 drew 562 M pixels in 118.5 s inside the draw, 4.7
Mpixels/s, which is the table's bilinear-with-Z world pass and blended
lightmap pass. Bilinear costs 2.1x point sampling and Z 40 % on top: at
640x480 the frame is the silicon's, and lower clocks under Velocity9x
are ruled out.

## Open

- 14,617 texture-fit skips and 98,754 degenerate pieces per 640x480 run,
  not analysed.
- Where the CPU is the limit (320x240, point sampling) setup is still
  ~20 k cycles a piece: per-trapezoid colour and Z planes, and the split
  decision's fit, are the next costs.
