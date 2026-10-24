# Rage 128: the 32 MiB map breaks 3DMark 2001 SE, and a second 3D program hangs A8U4I5

Date: 2026-10-25. Found on A8U4I5 (Rage Fury Pro, 5046, 440BX, Win98 SE)
after Michael reported that switching between 3D programs - HL1
Direct3D to OpenGL, or 3DMark 2000 then 3DMark 2001 SE - crashes or
draws wrongly until a reboot. 3DMark 2001 SE's error.log read "File
\DATA\GS1\KF\OBJECTS\TEXTURES\GAS_WALLSDOORS.JPG is corrupted".
Status: the map is back to 16 MiB (the corruption is not seen there);
the hang is open and its cause is not known.

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
| 602 | this fix | 16 MiB | 16 MiB | on, 16 MiB | no error; 3DMark gone by 75 s |
| 602 | same boot, started again | 16 MiB | 16 MiB | on | machine hung before Benchmark |

The error names a different texture file each run (GAS_WALLSDOORS.JPG,
HW_SIGNS.JPG, a .kf2); the data comes out of 3DMark's .ras archives.

## What the evidence says

- Write-combining is not the cause: corruption with NOWC (592, 596).
- The 32 MiB map is: corrupted 4 of 4 at 32 MiB, 0 of 2 at 16 MiB.
- It is the mapping, not surfaces placed above 16 MiB: with the
  DirectDraw heap capped at 16 MiB under the 32 MiB map, nothing is
  allocated up there, and the machine hung both times (599, 600).
- A hang exists without the 32 MiB map: 593 on the morning's build and
  602's second launch on the 16 MiB build. The 602 pair - one program
  ends silently, the next hangs the machine - is the reported symptom.

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

## Seen, not yet tied to the hang

After V9XD32 exits (boot 603, two runs, both PASS) the CCE is left in
bus-master ring mode, `PM4_BUFFER_CNTL` `8800000D`, with the microengine
free-running, `PM4_MICRO_CNTL` `40003FFF`, until the next 2D drain or
Lock stops it. Before any 3D on that boot it read `00000000` and
`400006D7`. A free-running microengine with nothing to drain it is what
hung A8U4I5 at boot 522
(`../decisions/2026-10-22-rage128-cce-pio-runs-packets-and-a-triangle.md`).

## Next

Reproduce without 3DMark (each hang needs a power cycle): two 3D
programs in sequence, one ending abnormally, with V9XR1MM between. Try
draining and stopping the CCE when a process's Direct3D context or
DirectDraw object goes away. Whether a 32 MiB map can come back depends
on finding why the mapping alone matters.
