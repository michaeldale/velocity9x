# Write-combining the GM965's and GM45's aperture: FbWrite 130 to 3,310 and 101 to 2,550 MB/s

Date: 2026-11-13

Status: implemented for the four Gen4 rows that lacked it (GM965, G45,
Q45, GM45) and measured on two of them: TOMMOHP (`M-KYHT4Q`, HP laptop,
Core 2 Duo T7250, GM965 GMA X3100 `8086:2A02`, desktop 1280x800x16),
boots 50-53, and an HP t5740 (`M-6B21TJ`, GL40 GMA 4500MHD `8086:2A42`,
desktop 1024x768x16), boots 33-34. Both through remote help. The G45 and
Q45 take the same line under the same-design rule and have not run it.

Evidence: [`../probe/gen4-write-combining-2026-11-13/`](../probe/gen4-write-combining-2026-11-13/).
What it extends: [2026-10-25-intel-gen3-write-combining.md](2026-10-25-intel-gen3-write-combining.md),
[2026-10-25-ironlake-write-combining.md](2026-10-25-ironlake-write-combining.md).

## How it was found

The GM965's texture pool Phase 0
([record](2026-11-13-intel-gm965-texture-pool-phase0.md)) showed no
`MtrrWrite` line in its boot record: its device row never set
`write_combine`. Ironlake, which runs the same Gen4 engine, has had it
since 2026-10-25. Nothing had measured that these four rows were left out.

## What the BIOSes leave (`V9XBOOT.INI`)

TOMMOHP: `n=8`, pair 0 WB 0-2 GiB, pair 1 UC 8 MiB at `7F800000` (stolen
memory), pair 2 UC 128 KiB at `FEDA0000`, pairs 3-7 free. The t5740:
`n=7`, pair 0 WB 0-2 GiB, pairs 1-3 UC over `7BD00000`-`7FFFFFFF` (stolen
memory), pairs 4-6 free. On both the GMADR at `D0000000` lies under the UC
default with no pair touching it.

## What changed

`write_combine` = 1 on `v9x_gm965_device`, `v9x_g45_x4500_device`,
`v9x_q45_gma4500_device` and `v9x_gm45_gma4500mhd_device`
(`gma950_hw16.c`). The window is the family's 16 MiB mapping. No drain was
added: these chips submit through `v9x_d3d_i9xx_ring_submit_mode`, whose
TAIL write has drained since 5ca96ad4, and `V9xHalFlip` and `V9xHalUnlock`
drain as the Gen3 record describes.

## Boots

| Machine | Boot | Build | `MtrrWrite=` |
|---|---|---|---|
| GM965 | 50 | df486bef-dirty (the Phase 0 64 MiB-map build), uncached | (no line) |
| GM965 | 51 | df486bef-dirty + the GM965 line | `ok s=3 b=d0000001 m=ff000800` |
| GM965 | 52, 53 | ab30f47c-dirty, all four lines | `ok s=3 b=d0000001 m=ff000800` |
| GM45 | 33 | released 0.49.0 (4357fc87), uncached | (no line) |
| GM45 | 34 | ab30f47c-dirty, all four lines | `ok s=4 b=d0000001 m=ff000800` |

All eight intel-gma files by `run-release-checks.ps1` (WININIT renames,
8 of 8 hashes matching), `reset`; ESC on TOMMOHP's network-password box.

## Measured (dispbench 95c470c, the desktop's own mode, 16 bpp)

| | GM965 b50 uncached | GM965 b51 WC | GM45 b33 uncached | GM45 b34 WC |
|---|---:|---:|---:|---:|
| FbWrite MB/s | 130.2 | **3,310.3** | 100.9 | **2,550.0** |
| FbRead MB/s | 31.3 | 217.4 | 27.2 | 27.4 |
| Gdi.FillRect, full screen, cyc/px | 30.65 | 1.21 | 31.72 | 1.88 |
| Gdi.BitBltDibToScreen, full screen, cyc/px | 30.77 | 1.86 | 32.51 | 8.40 |
| Gdi.TextOut.FixedOpaque cyc/string | 156,082 | 26,400 | 195,569 | 92,680 |
| Gdi.BitBltScreenToScreen.256x256 cyc/px | 205.69 | 25.93 | 183.08 | 129.30 |
| Dd.BltFast.SysToVid.Plain.512x512 cyc/px | 15.35 | 1.22 | 17.05 | 2.22 |
| Dd.BltFast.VidToVid.Plain.512x512 cyc/px (engine) | 3.57 | 3.58 | 2.34 | 2.32 |
| Dd.Lock.Primary cyc/lock | 3,333 | 3,333 | 9,478 | 9,514 |
| DB3D TexUpload.Lock cyc/byte | 30.71 | 2.63 | 32.78 | 5.37 |
| DB3D TexUpload.Blt cyc/byte | 7.70 | 0.59 | 8.29 | 0.95 |
| DB3D Clear.Target cyc/px (engine) | 1.52 | 1.52 | 1.67 | 1.67 |

One run each.

| | GM965 | GM45 |
|---|---|---|
| Probes, WC | boot 51: V9XD32, V9XGLP, V9XGLIDP, V9XR3DP, V9XWPRS, V9XFBCMP pass; boot 53: the same and V9XD32 /z24 | boot 34: V9XD32 default and /ddi5, V9XGLP, V9XGLIDP, V9XR3DP, V9XWPRS, V9XFBCMP pass; no verdict changed from the 0.49.0 checks |
| 3DMark 2000 default, WC | boot 53: 1224, 6.3 min, 5,687 flips, 521,624 draws, no refused batch, no engine stall, no draw into the presented page | not run |

Readings:

- **Aperture writes are 25 times faster on both chips**, and every
  CPU-drawn operation follows: full-screen GDI fills 17 to 25 times,
  texture uploads 6 to 13 times.
- **Reads rise sevenfold on the GM965 and do not move on the GM45.**
  Ironlake's tripled and the netbook's did not move. WC reads are not
  cached; why some platforms gain is not measured.
- **Engine blits and clears do not move**, as on every chip before.
- **3DMark 2000 on the GM965: 1224.** The scores recorded uncached on this
  machine are 825 (2026-10-21, a214e9d5) and 774 (2026-11-12, 0.48.0
  code). Different builds, so a direction, not a pair; recorded, not
  compared further.

## Not established

- **No control boot on the same binaries.** The Gen3 and Ironlake records
  ran `NOWC.TXT` boots; here the uncached column is the previous build on
  each machine (the GM965's differs by the 64 MiB map, the GM45's is the
  0.49.0 release). The off switch itself was not exercised on these chips.
- The first 3DMark attempt on the GM965 (boot 52) never started: the ESC
  presses sent for the logon box closed 3DMark's welcome dialog. Boot 53
  sent them earlier and ran.
- The GM45's OpenGL swap in V9XWPRS read 4.21 ms against 2.75 ms in the
  0.49.0 checks; one run, not looked into.
- The G45 and Q45: nothing has run on either on any build since 0.28.0's
  field report.
- A game on either machine.

## State left behind

TOMMOHP (boot 53) and the t5740 (boot 34) run `ab30f47c-dirty`, which is
this change over 0.50.0-dev main. 3DMark 2000 is open at its score window
on TOMMOHP. The dispbench tools are in `C:\DISPBENCH` on both.
