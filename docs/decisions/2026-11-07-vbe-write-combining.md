# Write-combining the vbe family's frame buffer: FbWrite 31.7 to 2,513 and 316 to 3,000 MB/s

Date: 2026-11-07

Status: implemented in `aadde512`. Measured on two remote-help AMD
machines running the `vbe` package at 1024x768x16, and on A8U4I5's
Pentium III at 800x600x16, build `77b45a4a-dirty` (the change, before
it was committed). One build per machine, one run each way.

Precedents, same mechanism: [Rage 128](2026-10-24-rage128-write-combining.md),
[SiS 6326](2026-10-29-sis6326-write-combining.md).

## What changed

- `V9X_HW16_DEVICE.write_combine` gains a second value. `V9X_HW16_WC_MAPPING`
  (1) is what every opted-in chip already used: the policy plans over the
  whole mapping, which that chip's maintainer checked lies in its BAR.
  `V9X_HW16_WC_VRAM` (2) plans over `v9x_vbe_vram_bytes`, the memory the
  BIOS reports through 4F00h, clamped to the mapping. That is the window
  Stage A has planned, and recorded as `Mtrr=`, on every machine since
  August.
- `v9x_vbe_device` opts in with `V9X_HW16_WC_VRAM`. That entry stands in for
  every card the family is installed on, so its BAR is unknown. The family's
  mapping is a fixed 16 MiB, which could run past a small card's BAR into
  another device. The memory the BIOS hands out through the linear frame
  buffer cannot.
- Nothing else. The family has no engine, so no hardware reads or writes the
  frame buffer behind the CPU, and the shared drains in Unlock and Flip are
  all it needs. `C:\V9XDIAG\NOWC.TXT` turns it off, as on every chip.

The 86Box guests were not used: their Pentium MMX has no MTRRs.

## The machines

| | M-JJN1ZZ (W9QI0DA2) | M-D7TFW4 (W9QI6DFC) |
|---|---|---|
| CPU | AMD GX-412HC, 1.2 GHz | AMD A10-5700, 3.4 GHz |
| Display | Radeon R3E, `1002:9854` (GCN 2), integrated | Radeon HD 8570, `1002:6611` (GCN 1), PCIe |
| VBE | 3.0, 16 MiB, LFB `e0000000` | 3.0, 16 MiB, LFB `c0000000` |
| Stage A plan | `r=0 s=5 b=e0000000 z=01000000` | `r=0 s=4 b=c0000000 z=01000000` |

On both, the BIOS's variable ranges cover RAM (write-back) and a few holes
(uncached), the default type is uncached, and no range touches the frame
buffer. So the WC pair is not overlapped by an uncached range, and no PAT
route was needed.

## The A/B

Install by WININIT rename and `reset`, all four driver files hash-checked
against the package. The off boot had `NOWC.TXT`; the on boot renamed it
away by WININIT and changed nothing else.

| | M-JJN1ZZ off (boot 10) | on (boot 11) | M-D7TFW4 off (boot 51) | on (boot 52) |
|---|---:|---:|---:|---:|
| Boot record | `MtrrWrite=off-marker` | `MtrrWrite=ok s=5 b=e0000001 m=ff000800` | `off-marker` | `ok s=4 b=c0000001 m=ff000800` |
| FbWrite MB/s | 31.7 | **2,513.1** | 315.7 | **2,999.8** |
| FbRead MB/s | 5.3 | 5.3 | 5.8 | 11.3 |
| FillRect 1024x768 | 31.6 | 2,513.8 | 315.5 | 2,980.0 |
| FillRect 64x64 | 31.2 | 1,057.2 | 275.1 | 1,406.4 |
| FillRect 16x16 | 25.5 | 104.1 | 93.8 | 117.0 |
| BitBlt bitmap to screen 256x256 | 31.4 | 2,361.3 | 313.9 | 2,858.0 |
| BitBlt DIB to screen 256x256 | 31.4 | 921.7 | 312.7 | 2,744.2 |
| BitBlt screen to screen 256x256 | 4.7 | 4.7 | 5.9 | 5.8 |
| Ellipse 128x128 | 27.0 | 215.1 | 181.7 | 355.2 |
| DirectDraw Blt system to video 512x512 | 62.3 | 2,468.6 | | |

dispbench `082ccfd`, `run-bench.ps1 -Mode 1024x768x16 -Tools
DBTIME,DBGDI,DBDD`. The runs are in dispbench `docs/results/`:
`m-jjn1zz-r3e-v9x-vbe/20261107-172025` (off) and `20261107-172812` (on),
`m-d7tfw4-hd8570-v9x-vbe/20261107-172320` (off) and `20261107-173014` (on).

The R3E's frame buffer is system RAM carved out for the GPU, so
write-combined stores reach memory speed (MemFill is 2,457 MB/s there).
The HD 8570 is behind PCIe, where the uncached figure was already ten times
the R3E's, and WC still gives 9.5 times.

## An Intel CPU: A8U4I5, Pentium III, Rage 128 Pro on the vbe package

Both machines above are AMD. A8U4I5 (Pentium III 1 GHz, 440BX, Rage 128
Pro 1002:5046 on AGP, VBE 2.0 reporting 32 MiB, LFB `dc000000`) ran the
same package in place of its `ati` driver, by WININIT rename, at
800x600x16, then went back to the `ati` files it had (boot 677,
hash-checked). Its BIOS ranges: write-back RAM, an uncached hole at
`d8000000`, a write-combined 32 MiB at `da000000`; none covers
`dc000000`. The plan was the window the 16 MiB mapping clamps 32 MiB to.

| | off (boot 675) | on (boot 676) |
|---|---:|---:|
| Boot record | `MtrrWrite=off-marker` | `MtrrWrite=ok s=3 b=dc000001 m=ff000800` |
| FbWrite MB/s | 49.9 | **240.1** |
| FbRead MB/s | 10.4 | 15.2 |
| FillRect 256x256 | 49.8 | 237.7 |
| FillRect 16x16 | 37.3 | 111.0 |
| BitBlt bitmap to screen 256x256 | 49.8 | 212.4 |
| BitBlt screen to screen 256x256 | 7.2 | 12.7 |
| Ellipse 128x128 | 38.9 | 103.9 |
| DirectDraw Blt system to video 512x512 | 92.1 | 250.3 |

dispbench `a8u4i5-rage128-v9x-vbe/20261107-174829` (off) and
`20261107-175220` (on), tools rebuilt at dispbench `2d62b16` (no code
change). On this slower bus the screen-to-screen copy also gained, since
half of it is the write.

## 86Box: an emulated Pentium II

Two 86Box 6.0 guests on the 686bx board with a Pentium II (Deschutes),
whose MTRRs 86Box emulates; the Pentium MMX guests have none. Both had
agent 0.5.2, updated to 0.16.0 by WININIT rename on the first boot. Both
were restored from backups afterwards
(`86Box VMs\Velocity9x Backups\*-pre-vbewc-20261107`).

- **Win98SE-BX-Voodoo3** (Voodoo3 3000 AGP, VBE 3.0, 16 MiB). It ran
  Windows' own VGA driver; the vbe package went on by Have Disk. Boot 286,
  `NOWC.TXT`: `enable-ok` at 640x480x8, `MtrrWrite=off-marker`, plan
  `r=0 s=3 b=da000000 z=01000000`. Boot 287: `MtrrWrite=ok s=3 b=da000001
  m=ff000800`, `enable-ok`, the desktop drawn correctly
  ([screenshot](../probe/86box-vbe-write-combining-2026-11-07/bx-voodoo3-desktop-wc-on-boot287.png)).
  V9XGDI produced no report on this guest, run either way, as on the
  Trio64V+ guest the same day; it is not counted. No timings: an
  emulator's frame-buffer cost is the emulator's.
- **Win98SE-BX-Trio64** (Trio64, 4 MiB). The plan line shows the window
  shrinking to the BIOS's memory, `b=dd000000 z=00400000`, the one case the
  three machines did not reach. It was never written: the vbe driver
  stopped at `Stage=fail-hardware-vbe-mode`, because this BIOS sets no
  linear-frame-buffer mode, which the family refuses
  ([2026-11-06-vbe-family-refuses-banked-modes.md](../issues/2026-11-06-vbe-family-refuses-banked-modes.md)).
  Unrelated to this change; the boot had `NOWC.TXT` and the failure is
  the mode set.

## What it does not change

The reads. Screen-to-screen copies are window moves and scrolling in GDI.
On the two AMD machines they stay at 4.7 and 5.9 MB/s, because WC memory is
not cached for reads; on the Pentium III they gain only the write half (7.2
to 12.7). On this family, with no engine, that is the part of "the desktop
feels slow" this change leaves. FbRead moved on M-D7TFW4 (5.8 to 11.3) and
A8U4I5 (10.4 to 15.2) in one run each way; it is not claimed.

## Not checked

- Any card but these three and the emulated Voodoo3. A card whose BIOS
  reports less memory than the 16 MiB mapping, where the window shrinks:
  planned on the emulated Trio64, never written. Any CPU without MTRRs or
  with a BIOS range over the frame buffer (the plan then refuses and the
  driver stays uncached, as before). Windows 95.
- A DOS box, a game, and the screen by eye. All three desktops came up
  `enable-ok` on the on boot and ran the three tools to completion.
