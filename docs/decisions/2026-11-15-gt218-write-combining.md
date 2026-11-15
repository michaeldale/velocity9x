# Write-combining the GeForce 210's BAR1 mapping: FbWrite 332 to 2,965 MB/s, engine results unchanged

Date: 2026-11-15

Status: measured; no code change beyond the row's comment. The
GT218 row has opted in with `V9X_HW16_WC_VRAM` since the
[BAR1 heap](2026-11-11-gt218-bar1-heap.md) work (boot 83 on M-BXMFA8),
which left its speed "installed but unmeasured". This is that measurement,
following [the plan](../plans/nvidia-gt218-write-combining.md).

Precedents, same mechanism: [vbe](2026-11-07-vbe-write-combining.md),
[Trinity](2026-11-08-trinity-write-combining.md),
[Rage 128](2026-10-24-rage128-write-combining.md).

## The machine and build

W9QI0DAD (10.0.1.172, sharing A8U4I5's address for now): AMD FX-6300
3.5 GHz, Gigabyte SB700 board, 2 GiB, ASUS GeForce 210 `10DE:0A65`
subsys `1043:83F4` rev A2, PCIe. Windows 98 SE, agent 0.18.0.

The nvidia package from origin/main `19ec1531`, DRV/VXD/HAL/SETP by
WININIT rename and `V9XGL.DLL` put directly, all hash-checked (boot 8).
Every run below uses those same files; only `C:\V9XDIAG\NOWC.TXT`
changed. Desktop 1024x768x16.

| | off | on |
|---|---|---|
| Boots | 13 (dispbench, probes), 16 (V9XD32 32 bpp, 3DMark) | 14 (dispbench, probes), 15 (3DMark) |
| `MtrrWrite` | `off-marker` (13, 16) | `ok s=3 b=c0000001 m=f8000800` (14) |
| `Mtrr3` before the write | empty | empty |

Boot 15's record was not captured (a script read it with `cat`, which
gave an empty string inside the script); its state is the marker's
absence, the same files and marker as boot 14. The BIOS's ranges are
WB RAM (MTRRs 0-1) and a 2 MiB UC hole at `BFE00000`; nothing covers
`C0000000`, so the pair is not overlapped.

## dispbench

dispbench `95c470c`, `run-bench.ps1 -ConfigId w9qi0dad-gt210-v9x-nvidia
-Environment metal -Mode 1024x768x16,1024x768x32 -Tools DBTIME,DBGDI,DBDD`.
Runs: off `20261115-190013` (16) and `-190247` (32); on `-192934` (16)
and `-193157` (32). DBTIME flags this CPU's TSC as unstable (10,000 ppm
spread, -4,973 ppm drift), so small moves are noise; the changes below
are many times that.

Cycles per pixel unless marked.

| | 16 bpp off | 16 bpp on | 32 bpp off | 32 bpp on |
|---|---:|---:|---:|---:|
| FbWrite MB/s | 331.7 | **2,965.1** | 330.0 | **2,976.5** |
| FbRead MB/s | 3.6 | 4.8 | 3.6 | 4.8 |
| FillRect 1024x768 | 21.30 | 2.36 | 42.57 | 4.70 |
| FillRect 64x64 | 24.05 | 5.24 | 45.21 | 7.47 |
| FillRect 16x16 | 68.92 | 59.15 | 83.83 | 57.84 |
| BitBlt bitmap to screen 256x256 | 21.40 | 2.46 | 42.71 | 4.80 |
| BitBlt DIB to screen 256x256 | 21.50 | 2.58 | 42.75 | 4.86 |
| StretchDIBits 256x256 | 58.11 | 40.14 | 43.18 | 5.33 |
| Ellipse 128x128 | 36.92 | 20.03 | 57.46 | 22.59 |
| TextOut fixed opaque, cyc/string | 114,732 | 26,974 | 216,815 | 35,793 |
| BitBlt screen to screen 256x256 | 2,072.6 | 1,969.6 | 4,147.6 | 3,941.0 |
| DD Blt system to video 512x512 | 10.90 | 2.46 | 21.68 | 4.81 |
| DD Blt video to video 512x512 (engine) | 5.05 | 5.03 | 5.59 | 5.58 |
| DD ColorFill video 512x512 (engine) | 2.56 | 2.57 | 3.36 | 3.35 |
| DD Lock after a video-to-video Blt, cyc/lock | 2,891,969 | 680,696 | 5,738,987 | 1,294,866 |

What the table says:

- CPU writes through BAR1 go from about one bus transaction a store to
  near the PCIe link's rate: 8.9 times at both depths, close to the HD
  8570's 9.5 times on a similar AMD host.
- The engine's own work (video-to-video Blt, ColorFill) is the same both
  ways, as it should be: the CPU only writes its pushbuffer.
- Lock after an engine Blt costs a quarter as much. The cause was not
  traced; the likely part is the pushbuffer and IB entry being written
  combined before the kick that Lock waits on.
- **GDI ran on the CPU in all four runs.** The GDI engine (plan step
  5.4, option A) uses the channel only once a DirectDraw program has built
  it, and dispbench ran first after each boot. These are CPU-drawing
  numbers, which is what write-combining changes. GDI with the channel
  live was not measured.
- Screen-to-screen GDI copies stay at about 2,000 (16 bpp) and 4,000
  (32 bpp) cycles a pixel: they are reads through BAR1 at 3.6-4.8 MB/s,
  which write-combining does not touch. With the channel live they would
  go to the engine. Worth its own look; not this record's change.

## The engine under write-combining

Same order each boot: V9XDDP, V9XD32 (32 bpp, then `/bpp16`), V9XGDI
`/auto`, then `/auto /accel` last (it poisons the session by design).

| | off | on |
|---|---|---|
| V9XDDP `Result` | COMPLETE | COMPLETE |
| V9XDDP `*Ok=` keys | 99 | 99, **identical** |
| V9XD32 32 bpp | FAIL, Failures=2 (T22X, T20 partial) | **identical** verdicts |
| V9XD32 16 bpp | FAIL, Failures=2 (T22X, T20 partial) | **identical** verdicts |
| V9XD32 idle/FIFO timeouts | 0 / 0 | 0 / 0 |
| V9XGDI `/auto` | PASS | PASS |
| V9XGDI `/accel` | PASS; 283 fills, 131 copies, 0 timeouts | PASS; 216 fills, 131 copies, 0 timeouts |
| 3DMark 2000 default | completes, 2956 | completes, 3948 |
| 3DMark engine timeouts, D3D rejects, texture refusals | 0, 0, 0 | 0, 0, 0 |
| `V9XN50F.TXT` (engine fault record) | absent | absent |

The 30 zero `Ok` keys and the two V9XD32 failures are the GT218's open
list from M-BXMFA8 (Z Lock, XRGB8888 texture, T20/T21), the same with
write-combining on or off. `/accel`'s fill count follows its seeded
stream and the number of declines; it is not a regression signal (both
runs pass their own reference comparison).

3DMark scores are recorded, not compared. Its frame grab
(`V9XTRACE -grab`) produced no files: the grab hooks Flip, and every flip
was declined (59,965 and 63,042), since this row has no display-start
flips yet. So no frame was inspected; the evidence is the absence of
engine timeouts, rejects and fault records, and identical probe verdicts.

## Decision

Keep write-combining on the GT218 row. The drains already in the tree
(`eng_nv50.c` before every kick, `V9XWCDRAIN` in the 16-bit GDI arm, the
shared Unlock and Flip drains) are enough: nothing that passes uncached
fails combined. The row's comment changes from "unmeasured" to this
record; no other code changes.

By the same-design rule this covers M-BXMFA8's GeForce 210 (same row,
same code), which ran with this pair on for boots 83-127 already.

## Not settled here

- GDI throughput with the channel live (after a DirectDraw program), with
  WC on: not measured.
- Screen-to-screen GDI copies are CPU reads at ~2,000-4,000 cycles a
  pixel until the channel exists.
- Frame correctness under 3DMark was not inspected (no grabs without
  flips).
- Windows' own restart hangs on this machine at a flashing cursor since
  boot 12 (boots 13 and 14 took 8-10 minutes, recovered by hand); `reset`
  is used instead. Not investigated; the USB changes of the same day and
  the driver's shutdown path are both candidates.
