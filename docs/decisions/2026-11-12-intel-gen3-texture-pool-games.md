# Intel Gen3 texture pool: Half-Life, Quake 2 and Serious Sam on the netbook; Serious Sam's textures now fit, with no failed creates or evictions

Date: 2026-11-12
Machine: MICHAEL-NETBOOK (945GSE), boot 319, build `3c7d7ab6` (the Phase 3
commit; installed by the release runner as `b51e96d4-dirty`, the same
code).
Follows: `2026-11-12-intel-gen3-texture-pool-phase3.md`, whose "Not
established" list this works through.
Evidence: `docs/probe/gen3-texture-pool-games-2026-11-12/`.

## Results

| Game | API | Result |
|---|---|---|
| Half-Life, `timedemo mwd5`, windowed 640x480, three runs | Direct3D | 45.7, 56.8, 56.4 fps; the 2026-11-07 runs on this machine were 46.4, 56.6, 56.6 (recorded, not compared) |
| Quake 2 demo, demo1 spawn, windowed 640x480, `notarget`, `timerefresh` | OpenGL (ICD, SGIS multitexture) | spawn draws correctly; 64.7 fps (recorded) |
| Serious Sam FE 1.05, `auto-demo0003` from the Demo menu, 640x480 fullscreen | OpenGL (ICD) | demo drew six ten-second intervals at 31,931 to 38,398 draws, about 9 fps, every textured batch on the GPU |

No hang, fault or mode-change failure in about 80 minutes of this boot,
which also ran 3DMark 2001 SE and the release probes (Phase 3 record).

## Serious Sam's textures

The ICD's counters (`ss-V9XGL-demo-excerpt-boot319.log`) across the demo:

| | this run | 2026-10-02 `ss2` (`auto-demo0001`) |
|---|---|---|
| live hardware textures, peak | 488 | not recorded |
| creates | 533 in the demo | 2,985 per ten seconds |
| failed creates | **0** | 1,494 per ten seconds |
| evictions | **0** | 1,487 per ten seconds |
| textures too big for the hardware path | 0 | 649 per ten seconds (`ss3`) |
| batches drawn by the CPU | 0 | 427 per ten seconds |
| uploaded | about 20 MB in all | 23.4 MB per ten seconds |

The demos differ (`0003` here, `0001` then) and so do the builds: the
256-texel ceiling behind October's `too-big` went with the 2048 limit
(2026-11-11), not with the pool. What the pool answers is the other half:
October's failed creates were `DDERR_OUTOFVIDEOMEMORY` with the cache
churning; here 488 textures stayed resident and nothing failed or was
evicted.

The HAL's own counters did not move across Half-Life's run and Serious
Sam's: `MipTreeDeclined` 118 and `D3dTextureRefusedBounds` 55 are 3DMark
2001 SE's, from before. Half-Life's mip trees went into the pool
(`MipTreeLastOffset=0x00B62000`, 3.4 MiB into it).

## How the Serious Sam run was driven, and what went wrong first

Three attempts failed before this one, none for the driver:

- `SERIOUSSAM.log` is buffered: its lines reach the disk minutes late or at
  exit, so the agent's `wait-file` on it cannot see the menu or a demo
  start. The ICD's `V9XGL.LOG` is written line by line; its ten-second
  `tsc` lines (draws above 1,000 in the demo, about 100-200 at the menu)
  are the signal to watch.
- A backtick typed after `PlayDemo` went into the console line (`parse
  error`) and left the console open, which pauses the world: run 1 drew
  one triangle batch a frame for eight minutes.
- ESCAPE closes the console but also stops the demo; the intro level
  aborted twice by itself (`Cannot start game: User break!`).

The run that worked used the main menu's Demo entry by mouse.

## Not established

- The DOS-box round trip with the pool mapped: left for a run with
  Michael at the machine, as the plan says, since a hang there needs a
  power cycle.
- The Dragothic sky
  (`docs/issues/2026-11-07-gma950-3dmark2001se-dragothic-sky-white.md`):
  not looked at; it needs a frame grab of that test alone.
- `auto-demo0001`, the demo October measured: the console route to it
  failed as above.
