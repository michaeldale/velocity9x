# Haswell texture pool, Phase 4: mapped at the first Enable, a full 3DMark 2001 SE (4517) and UT2004 run on it, and vsync-off Car Chase loses its stalls too

Date: 2026-11-19. Machine: the i5-4570, HD 4600, boot 288 (a fresh
`reset`), the Phase 3 build (`15db3df7`'s code). Plan:
[intel-haswell-system-memory-textures.md](../plans/intel-haswell-system-memory-textures.md),
Phase 4. Phase 3: [record](2026-11-19-haswell-texture-pool-phase3.md).
Evidence: [`../probe/hd4600-texture-pool-phase4-2026-11-19/`](../probe/hd4600-texture-pool-phase4-2026-11-19/).

## Results

| Check | Result |
|---|---|
| First Enable after a reset (`INTELGTT.TXT`) | `StateBefore=0`, `Result=mapped`, pool at `02000000h`, 32 MiB: the driver maps it itself, not only keeps it |
| 3DMark 2001 SE, full default run, vsync on | completed in 7.9 minutes, **score 4517**; 0 mip trees refused, 1,020 placed, 0 engine resets or idle timeouts; ten grabs across Car Chase and Dragothic look right |
| UT2004 demo botmatch (`dm-rankin`, 12 bots, Michael's saved settings) | ran to its end (`appRequestExit(0)`), 0 mip trees refused, 0 engine resets; 40.25 fps average, seed 11503 |
| 3DMark 2001 SE, vsync off (copy-present), 2 minutes, OBS | 0 mip trees refused (354 without the pool), 8,134 flips (6,066), every flip held to the hardware; meter Car Chase Low 76, 106, 12, 121, 94, 130 (1, 38, 42, 6, 1, 6 without the pool), High 76, 40, 37, 79, 30, 58 |

The score and the UT2004 average are recorded, not compared: the last full
3DMark runs in the notes (3,388-3,888, 2026-11-18) and the last seed-11503
botmatch (38.05) ran older builds, and the render clock has changed between
them. UT2004's log warns that `DemoPlayerSkins.DemoSkeleton` is corrupted;
15 of its 20 logs from 2026-11-13 and 14, before the pool, carry the same
line: the game's own package, not the driver.

## Not run

- A DOS box with the pool mapped: DOS programs are not run through the
  agent; it needs Michael at the machine.
- Mode changes beyond 3DMark's and UT2004's own (Phase 2 measured that mode
  sets leave the PTEs alone).
- Quake 3 and the OpenGL ICD's textures in the pool.
- Any other Haswell or Ivy Bridge: the gate refuses them. Lifting it is
  decision 3's next step, after the release checks pass on the i5-4570.
