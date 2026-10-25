# Rage 128: the AGP CCE ring corrupts 3DMark 2001 SE's data, and a second 3D program hangs A8U4I5

Date: 2026-10-25. Found on A8U4I5 (Rage Fury Pro, 5046, 440BX, Win98 SE)
after Michael reported that switching between 3D programs - HL1
Direct3D to OpenGL, or 3DMark 2000 then 3DMark 2001 SE - crashes or
draws wrongly until a reboot. 3DMark 2001 SE's error.log read "File
\DATA\GS1\KF\OBJECTS\TEXTURES\GAS_WALLSDOORS.JPG is corrupted".
Status: parked. The CCE runs by PIO again, as in 0.28.0, with one texture
unit (two-unit triangles need the ring, below); the ring code stays,
unused. The engine is drained and the CCE stopped when a Direct3D
or OpenGL context goes. Neither cause is known.

The file name is from the first, wrong conclusion that the 32 MiB map
was the cause; the 16 MiB rows below were luck. The map stays at 16 MiB
only because 32 MiB has not been run under PIO.

## Runs

3DMark 2001 SE, Benchmark clicked, error.log compared after 75 s. Each
row is a fresh boot unless noted. `NOWC` is `C:\V9XDIAG\NOWC.TXT`, which
stops the MTRR write.

| Boot | Build | Map | Heap | WC | Result |
|---|---|---|---|---|---|
| 591, 595 | 41f0ba18 | 32 MiB | 32 MiB | on | file "corrupted" |
| 592 | 41f0ba18 | 32 MiB | 32 MiB | off (NOWC) | corrupted |
| 593 | 5ef364f9 (before 5fcc5a7f) | 16 MiB | 16 MiB | none | machine hung |
| 596 | 5fcc5a7f | 32 MiB | 32 MiB | off (NOWC) | corrupted |
| 597 | ATI 4.13.7192 | - | - | - | runs |
| 598 | 41f0ba18, map 16 MiB | 16 MiB | 16 MiB | off (NOWC) | runs, Game 1 draws |
| 599, 600 | 41f0ba18, map 32, heap capped 16 | 32 MiB | 16 MiB | off (NOWC) | machine hung, 2 of 2 |
| 602 | 177c9279 | 16 MiB | 16 MiB | on, 16 MiB | no error; 3DMark gone by 75 s |
| 602 | same boot, started again | 16 MiB | 16 MiB | on | machine hung before Benchmark |
| 605 | + context drain, ring | 16 MiB | 16 MiB | on | corrupted, 2 of 2 back to back; no hang |
| 606 | + context drain, PIO only | 16 MiB | 16 MiB | on | no error 2 of 2; run 2 drew Game 1; run 1 gone by 75 s |

All rows but 597 and 606 ran the CCE on the AGP ring (87ed9aa3 on).

The error names a different texture file each run (GAS_WALLSDOORS.JPG,
HW_SIGNS.JPG, a .kf2); the data comes out of 3DMark's .ras archives.

## What the evidence says

- Write-combining is not the cause: corruption with NOWC (592, 596).
- The map is not either. 177c9279 put it back to 16 MiB on 4 of 4
  corrupt at 32 MiB against 0 of 2 at 16 MiB; boot 605 then corrupted
  2 of 2 at 16 MiB.
- The ring is the one change that separates them: the same build at
  16 MiB corrupted with the ring (605) and did not with PIO (606). Two
  runs each; not proof.
- A hang exists on every map: 593 on the morning's build, 599 and 600,
  and 602's second launch. The 602 pair - one program ends silently,
  the next hangs the machine - is the reported symptom. After the
  context drain, the 605 pair did not hang; two runs.
- 3DMark gone by 75 s with no error in run 1 of a pair (602, 606) is not
  explained, and may be the script's launch timing.

## Hypotheses killed

- **The card's AGP window inside VRAM.** `AGP_APER_OFFSET` (0178h) reads
  `02000000` (V9XR1MM, boot 598): AGP space starts above all 32 MiB.
- **Offset fields too narrow.** 2D pitch/offset fields are 21 bits of
  32-byte units (64 MiB); the CRTC offset mask is 25 bits.
- **Scratch-register write-back DMA.** The Rage 128 has no
  `SCRATCH_UMSK`/`SCRATCH_ADDR` (Linux r128_drv.h, Mesa r128_reg.h);
  the age markers in `GUI_SCRATCH_REG2` are read back over MMIO only.
- **A second ring writer.** Only V9XHAL links `r128_cce.c`; the ICD does
  not keep its own write pointer.
- **Stray Direct3D writes off the target.** The 3D scissor is set to the
  target's edges (`d3d_r128_map.c`) and vertices are held to the guard
  band.
- **Overrun of the AGP vertex area.** Grants stay inside one 64 KiB
  segment of the 512 KiB area; both writers size their chunks from the
  vertex size they write (8 or 10 dwords); the mini-VDD reserves the
  145 pages r128_ring.h expects.

## The CCE left running after a program, and the drain

After V9XD32 exits (boot 603, two runs, both PASS) the CCE was left in
bus-master ring mode, `PM4_BUFFER_CNTL` `8800000D`, with the microengine
free-running, `PM4_MICRO_CNTL` `40003FFF`, until the next 2D drain or
Lock stopped it. Before any 3D on that boot it read `00000000` and
`400006D7`. A free-running microengine with nothing to drain it is what
hung A8U4I5 at boot 522
(`../decisions/2026-10-22-rage128-cce-pio-runs-packets-and-a-triangle.md`),
and a mode set against one hung it at boot 532.

Draining at context destroy alone did not change it (boot 604): a flip
after the destroy - DirectDraw's flip back to the GDI surface at exit -
queued through the ring and started the CCE again. With flips queued
only while the CCE already runs, it reads `08000000` and `00003FFF`
after both V9XD32 runs (boot 605), which pass at DDI 5 and 6.

## Two texture units need the ring

With PIO, 3DMark 2000 at DDI 6 hard-locked A8U4I5 at the start of its
benchmark three times (boots 607, 608, 611; 611 with the flip's
`WAIT_UNTIL` left out, which did not matter). The ring build with the
drain completed it (610, score 1649). The same PIO build with the Rage
128 reporting one texture stage completed it at DDI 6 (613, 1015) and
at DDI 5, the default with one stage (614 build, 1122).

Under the ring, every two-unit batch went through the AGP vertex area
(`3D_RNDR_GEN_INDX_PRIM`); under PIO they go inline as
`3D_RNDR_GEN_PRIM` with `VC_FRMT_S2_T2`, which only V9XD32 T12's two
triangles had sent. Neither Linux nor Mesa sends two-unit vertices
inline. Whether the microcode takes them inline at all is not known.
The Rage 128 reports one unit and one stage until the ring returns.

One DDI 5 run on the two-stage PIO build (609) drew Game 1 and then
3DMark stopped with a C++ exception (`E06D7363` in MSVCRT), with no
engine timeout or refused call in the HAL's counters. It did not recur
on the one-stage build.

## Suspect, not measured

The GART pages behind the ring: reserved `PG_WRITECOMBINED` and
committed with flags 0 (loader.asm), and the CPU writes the ring and
vertices through the aperture's linear mapping. If the frames behind
those GART entries can be reused by the VMM, the CPU's ring writes land
in whoever owns them now - the disk cache would read back as a corrupt
file - and the card fetches garbage, which would hang it. The DDK's
`_GARTCommit` takes no locking flag, so the headers do not settle it.

## Next

A probe that, during a 3DMark 2001 SE load, compares the physical
frames in the reservation's page-table entries against the 440BX GART
entries for the same pages. Then: the ring back as the transport, and
the 32 MiB map run under PIO.
