# 3D WinBench 98 picks a 16-bit Z buffer even when 32 is offered; its Z accuracy tests still fail

2026-10-18, MICHAEL-NETBOOK (GMA 950), boot 211, driver `a1d7cc1e-dirty`
(= c9529221: 24-bit depth offered beside 16). Step 4 of
[`../plans/intel-24-bit-depth.md`](../plans/intel-24-bit-depth.md): re-run
the three depth tests from September's failures
([2026-09-24-3d-winbench-98-quality-on-the-netbook.md](2026-09-24-3d-winbench-98-quality-on-the-netbook.md),
pass 3). Evidence:
[`../probe/netbook-3dwb98-z-2026-10-18/`](../probe/netbook-3dwb98-z-2026-10-18/).

## How it was run

A three-test suite (`ZDEPTH.ZDS`: Quality 4 Z-buffer, 38 Narrow Z
Accuracy, 39 Wide Z Accuracy) loaded through Suite > Load and run twice
with the defaults: Direct3D HAL, execute buffers, front buffer, full
screen at 640x480. The first run used Hi Color (16 bit) and the second
True Color (32 bit). Verdicts come from the benchmark's verdict page, which
shows the test's last frame beside a good and a bad reference. As in
September, the panel itself was not watched.

**Vsync had to be turned on first.** The netbook's SYSTEM.INI carries
`[Velocity9x] VSync=2` (always off, from the vsync-off work), and the first
attempt was refused: "The refresh rate of 25285 Hz indicates Flip is not
waiting for vertical sync. 3D WinBench will not give a result"
(`vsync-off-refused.png`). VSync was set to 0 (game decides) for the runs,
which then reported 60 Hz, and the original SYSTEM.INI was put back
byte-identical afterwards.

## What it chose, and what it drew

| Colour | Note 5 | `D3dDepthPitch` (640 wide) | Z-buffer | Narrow Z | Wide Z |
|---|---|---|---|---|---|
| 16 bit | "Z buffer depth: 16 bits" | 1280 (2 bytes) | Capable | Incorrect | Incorrect |
| 32 bit | "Z buffer depth: 16 bits" | 1280 (2 bytes) | Capable | Incorrect | Incorrect |

- **The benchmark asks for a 16-bit Z buffer at both colour depths**, with
  DDBD_16 | DDBD_32 in the device's `dwDeviceZBufferBitDepth`. The same
  boot's V9XD32 `/z24` run created and used a 32-bit Z surface, so
  DirectDraw does allocate one on request. The benchmark simply does not
  request it. So the 24-bit format was never exercised here, and neither
  accuracy result says anything about it.
- Narrow Z: the left pair intersects in a sawtooth (`b16-narrow-z-zoom.png`)
  and the right pair is straight, as in September. Wide Z: the left pair's
  blue cube is missing, as in the bad reference and in September. Both runs
  look the same.
- **New: the suite ran at 640x480x32 on the Gen3 HAL** (note 4, "640x480x32bpp"),
  with Z-buffer Capable. That is the first title-like workload on the
  32 bpp hardware path (a1d7cc1e): 28 contexts, about 34,500 draws on the
  engine over the session, no context rejects, no breadcrumb or idle
  timeouts (`V9XSNA7-b32-end.ini`).

## What this leaves

- This benchmark cannot judge 24-bit depth on this driver, because it never
  asks for it. Why it settles on 16 is not established. It may take the
  lowest depth in the caps, or it may ask for 16 outright. Its own
  "Z buffer depth" note suggests it records a choice. The Narrow and Wide
  results stay explained by 16-bit Z, as diagnosed in September, and are
  not a defect in the 24-bit work.
- What would test 24 bits in a title: one that asks for a 32-bit (or
  24-bit) Z surface. Whether offering DDBD_24 as well would change this
  benchmark's choice is untested; it would also mean a 3-byte Z surface,
  which nothing here supports.
- The September note that the Narrow and Wide failures need Z24 stands,
  but the remedy cannot be shown with this suite unless it can be made to
  ask for 32 bits.
