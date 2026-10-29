# Write-combining the SiS 6326's frame buffer: FbWrite 49.9 to 89.3 MB/s

Date: 2026-10-29

Status: implemented in `0524e7d1`. Measured on A8U4I5 (Pentium III 1 GHz,
440BX, Win98 SE) with SiS 6326 card 2 (rev 0Bh, 4 MiB SGRAM, AGP), at
boots 649 and 650, 800x600x16. One build, one machine, one run each way.

Precedent: [2026-10-24-rage128-write-combining.md](2026-10-24-rage128-write-combining.md),
same machine, same mechanism.

## What changed

- `v9x_sis6326_device.write_combine = 1`. The family maps 4 MiB, which is
  BAR0 whole: the frame buffer and nothing else. The engine registers are
  BAR1, mapped apart by the mini-VDD and left uncached. The Turbo Queue is
  off, so no command words sit in VRAM.
- Two drains, besides the shared ones in Unlock and Flip: before each 2D
  blit (`v9x_sis_emit`), because GDI writes the screen without a Lock, and
  before each 3D batch's state writes (`v9x_d3d_sis_draw`).

## The A/B

One package, `0524e7d1`, deployed by WININIT rename.

- **Boot 649: write-combining off.** `C:\V9XDIAG\NOWC.TXT` was present, and
  the boot record shows `MtrrWrite=off-marker`. The plan line reads
  `s=3 b=de000000 z=00400000`: pair 3, 4 MiB at the BAR0 base.
- **Boot 650: write-combining on.** `NOWC.TXT` was renamed to `NOWC.SIS` by
  WININIT. The boot record shows `MtrrWrite=ok s=3 b=de000001 m=ffc00800`:
  WC, 4 MiB, written and read back. The desktop came up enable-ok.

dispbench `bac206e`, `run-bench.ps1 -Mode 800x600x16 -Db3dDepths 16`, all six
tools. The runs are in dispbench `docs/results/a8u4i5-sis6326-v9x/`, stamps
`20261029-185433` (off) and `20261029-190337` (on).

| | WC off | WC on |
|---|---:|---:|
| FbWrite MB/s | 49.9 | **89.3** |
| FbRead MB/s | 11.4 | 12.8 |
| Gdi.FillRect.800x600 cyc/px | 40.23 | 22.42 |
| Gdi.PatBltSolid.800x600 cyc/px | 40.16 | 22.47 |
| Gdi.BitBltDibToScreen.800x600 cyc/px | 41.69 | 23.40 |
| Gdi.SetDIBitsToDevice.16TopDown.800x600 cyc/px | 41.73 | 23.38 |
| Gdi.TextOut.FixedOpaque cyc/string | 205,119 | 118,671 |
| Gdi.BitBltScreenToScreen.128x128 cyc/px | 256.14 | 220.78 |
| Dd.BltFast.SysToVid.Plain.512x512 cyc/px | 22.02 | 23.74 |
| Dd.Blt.VidToVid.Plain.512x512 cyc/px (engine) | 24.03 | 24.03 |
| Dd.Lock.OffscreenVid cyc/lock | 7,830 | 5,970 |
| DB3D TexUpload.Lock cyc/byte | 40.26 | **11.31** |
| DB3D TexUpload.Blt cyc/byte | 10.76 | 11.91 |
| DB3D FillTex16Bilinear cyc/px | 50.20 | 50.22 |
| DB3D TriBatch.512 cyc/tri | 7,488 | 7,463 |
| DB3D Clear.Target cyc/px | 8.19 | 8.19 |

## Readings

- **CPU writes to the card are 1.8x faster, not the Rage 128's 4.8x.**
  FbWrite 49.9 to 89.3 MB/s. On the same 440BX the Rage 128 reached
  241 MB/s write-combined. Uncached, both cards start at the same 49.9.
  So the ceiling is set by the card, through its bus interface, not by
  the CPU's write path. Why the SiS stops at 89 is not measured.
- **GDI's CPU drawing follows FbWrite.** Fills, DIB-to-screen and
  SetDIBitsToDevice drop from about 40 to about 22.4 cycles a pixel; text
  is 1.7x faster. They are CPU writes on this family (no GDI acceleration),
  so they move with the ceiling and no further.
- **Texture upload through Lock is 3.6x faster:** 40.3 to 11.3 cycles a
  byte, about 25 to 89 MB/s.
- **Upload by Blt, and SysToVid blits, did not move** (a few percent
  slower). They already ran at about 87-91 MB/s uncached, above the old
  FbWrite ceiling, so that path was never bound by uncached CPU stores. What
  bounds it is not established.
- **3D does not move.** Fill, triangle setup, clears and flips are within
  0.5%. The SiS 3D engine is fed through MMIO registers in BAR1, which stay
  uncached, and it draws into VRAM itself.
- **Engine blits are identical** (24.03 cycles a pixel both ways). dispbench
  now lists VidToVid as CLAIMED-NOT-DELIVERED in the WC run. That is its
  classifier judging the same timing against a higher FbWrite ceiling, not
  a change in the driver.

## What the evidence disputes or leaves open

- **DBDD died once, in the WC run, after its last test.** Every one of its
  80 verified checks reads `Verified=yes`. The report ends before
  `[Result]`, so run-bench marks the tool `died`. The Rage 128's first WC
  run did the same in DB3D (its record, "What the evidence disputes"). That
  is now two cards, two tools, once each, both write-combined, both at
  teardown. A missing drain on a teardown path would look like this. So
  would something unrelated to write-combining. DBDD was not rerun.
- **Two DB3D failures appear in both runs:** FillTexMipNearest's
  verification and FlipInterval. They do not come from write-combining. Not
  investigated here.
- **Applications, run later on the same boot 650**
  ([evidence](../probe/a8u4i5-sis6326-wc-apps-2026-10-29/README.md)):
  - V9XDDP completes; nothing that passed on 2026-10-05 fails.
  - V9XGLP passes.
  - Final Reality runs to its score (3D 1.90).
  - Half-Life runs under Direct3D (7.84 fps) and OpenGL (6.64 fps).
  - The boot shows no engine timeout and no declined flip.
- **3DMark 99 Max scored 200** against 274 on 2026-10-05. Not explained.
  The colour blend factors (draws once dropped are now made), 24 days of
  shared core changes and write-combining all lie between the two runs, and
  no run with write-combining off was made.
- **The console text in Half-Life and Quake 2 was wrong.** Its cause is not
  write-combining but the engine's missing U prestep, fixed separately
  ([record](2026-10-29-sis6326-u-prestep.md)).
