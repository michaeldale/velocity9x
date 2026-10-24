# Write-combining the Rage 128's frame buffer: the first MTRR this driver writes

Date: 2026-10-24

Status: implemented for the Rage 128 only, measured on A8U4I5 (Pentium III,
Rage 128 Pro 5046, Win98 SE) at boot 576, one boot. Every other family
still stops at Stage A: it reads the MTRRs and writes none.

Plan: [write-combining-stage-b.md](../plans/write-combining-stage-b.md).
Stage A: [2026-08-28-mtrr-stage-a-inspect-only.md](2026-08-28-mtrr-stage-a-inspect-only.md).
Why the Rage 128 went first: [rage128-dispbench-review.md](../plans/rage128-dispbench-review.md), item 1.

## What was measured before

dispbench `DBTIME` on A8U4I5 at 800x600x16, the same desktop under both
drivers:

| | FbWrite MB/s | FbRead MB/s |
|---|---:|---:|
| ATI 4.13.7192 (run 20261024-171143) | 242.2 | 15.2 |
| Velocity9x, no MTRR written (run 20261024-162442) | 49.9 | 10.4 |

The BIOS leaves A8U4I5's MTRRs like this (`V9XBOOT.INI`, every boot):

```
Mtrr=cpu=000f cap=00000508 def=00000c00 n=8 r=0 s=3 b=dc000000 z=02000000
Mtrr0=00000006 f0000800    WB, 0 - 256 MiB
Mtrr1=d8000000 fe000800    UC, 32 MiB at D8000000
Mtrr2=da000001 fe000800    WC, 32 MiB at DA000000 (the AGP window)
Mtrr3..7 = 0               free
```

The frame buffer, at `DC000000`, falls under the UC default. ATI's driver
writes a WC range over it itself; Velocity9x never did.

## What changed

- **The write** is in the mini-VDD (`V9xMini_Mtrr_Write`, `loader.asm`),
  reached through a new PM API function, `V9XMINI_FN_MTRR_WRITE` (0025h).
  It follows the SDM 11.11.7.2 sequence the plan names. It refuses (with a
  reason code) unless the pair is currently invalid, the base is typed WC,
  the mask is valid, and `DEF_TYPE` is enabled with a UC default. It reads
  the pair back before reporting success. `check-tree.ps1` now requires
  every `WRMSR` in `loader.asm` to sit inside that one routine.
- **Who calls it:** the 16-bit driver, at the end of the Stage A trace in
  `enable16.c`. This is not the plan's `Device_Init` design. Stage A's
  policy runs on the 16-bit side, so the plan is already known there; the
  VxD interprets nothing and only writes the pair it is handed.
- **The policy** (`mtrr.c`) is unchanged apart from
  `v9x_mtrr_wc_present`. When a WC pair already covers the window, the
  driver writes nothing (`MtrrWrite=present`); that is what a second
  `Enable` in the same session sees.
- **Opt-in per device:** `V9X_HW16_DEVICE.write_combine`, set only by the
  Rage 128. Its `map_bytes` goes from 16 to 32 MiB. BAR0 is 64 MiB, two
  32 MiB views of the same memory, so the first view is the whole card.
  Stage A's window then plans 32 MiB, one pair.
- **The drain:** `lock or dword ptr [esp],0`, a locked read-modify-write,
  before the CCE submit (`v9x_r128_emit`) and in `V9xHalUnlock`. It runs
  wherever the engine reads data the CPU has just written. A8U4I5 has SSE,
  but the locked operation drains on every CPU this HAL can load on, so it
  is not chosen by CPUID.
- **Kill switch, decision 1 option B:** `C:\V9XDIAG\NOWC.TXT` existing
  means no write (`MtrrWrite=off-marker`). `V9X3D OFF` creates it, and
  `RECOVER.TXT` gives the one DOS line that does the same.

## What boot 576 reports

```
DpmiMap=02000000:ok
MtrrWrite=ok s=3 b=dc000001 m=fe000800
```

That is pair 3, WC from `DC000000` for 32 MiB, as written and read back.
The `Mtrr3=` line above it is the readout from before the write. The
desktop drew correctly at 800x600x16. Nothing went wrong through dispbench
(DBCAPS, DBTIME, DBGDI, DBDD, DB3D twice) or a 3DMark 2000 default run.

## What was measured after (boot 576, dispbench c68d365)

Run 20261024-180226 (DBGDI, DBDD), 20261024-181326 (DB3D), against the
two runs above:

| | ATI | V9x before | V9x WC |
|---|---:|---:|---:|
| FbWrite MB/s | 242.2 | 49.9 | **241.4** |
| FbRead MB/s | 15.2 | 10.4 | 15.2 |
| Gdi.FillRect.800x600 cyc/px | 1.07 | 40.16 | 8.34 |
| Gdi.PatBltSolid.800x600 cyc/px | 1.07 | 40.19 | 8.32 |
| Gdi.BitBltDibToScreen.800x600 cyc/px | 9.96 | 41.71 | 9.94 |
| Gdi.SetDIBitsToDevice.16TopDown.800x600 cyc/px | 10.06 | 41.79 | 10.04 |
| Gdi.TextOut.FixedOpaque cyc/string | 10,977 | 204,133 | 52,124 |
| Gdi.BitBltScreenToScreen.128x128 cyc/px | 2.55 | 277.52 | 176.90 |
| Dd.BltFast.SysToVid.Plain.512x512 cyc/px | 13.48 | 21.76 | 8.25 |
| DB3D TexUpload.Lock MB/s | 248.9 | 24.9 | **247.0** |
| DB3D TexUpload.Blt MB/s | 1,372.7 | 98.5 | 216.6 |
| 3DMark 2000 default | 2404 | 1697 (H3, 5ef364f9) | 1694 |

Readings:

- **CPU writes to the card now match ATI.** FbWrite, DIB-to-screen,
  SetDIBitsToDevice, SysToVid blits and texture upload through Lock all
  land within 1% of ATI's driver. Before, they were 4-10x slower.
- **GDI fills are 5x faster but still 8x off ATI.** They are CPU writes
  under both Velocity9x builds and engine fills under ATI. Write-combining
  removes the uncached penalty but cannot replace the engine. That remains
  item 2 of the dispbench review: Rage 128 GDI acceleration.
- **Screen-to-screen copies gain only 30-50%.** They read the frame
  buffer, and reads stay uncached at 15 MB/s. Only the engine fixes them.
- **3DMark does not move.** Its game tests were already measured as
  chip-bound. Texture uploads at load time are faster, and no frame rate
  depends on them.

## What the evidence disputes or leaves open

- **Lock cost went up, not down:** `Dd.Lock.Primary` measures 5,679
  cycles per lock before and 6,558 after. The added drain is one locked
  instruction in Unlock, not 900 cycles. The before run was the reverted
  H4 build, not 5ef364f9 (see the review, section 0), so this is two
  variables at once. Not explained.
- **The before run's `Clear` and `Flip` figures** come from that same H4
  build. So DB3D's Clear (234 to 925 Mpix/s) and Flip.Vsync (60 to 64 fps)
  are not attributed to write-combining here.
- **The first DB3D run on boot 576 died after its last test**, before it
  wrote `[Result]`. No process was left running and no dialog was shown.
  The immediate rerun completed in 29 s. Seen once, not reproduced, cause
  unknown. It is recorded here because a teardown fault is exactly where
  a missing drain would show.
- **One boot, one machine.** No game was played under WC. Corruption was
  watched only through what the benchmarks verify themselves (DB3D read-back,
  DBGDI/DBDD verdicts), not by the operator.
- **Other families** are untouched. The Intel GMADR case the plan was
  written for (netbook, ring writes) still needs its own opt-in, drain
  points and boot.
