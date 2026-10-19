# Half-Life on the Mystique: OpenGL and Direct3D draw almost all on the engine

Date: 2026-10-19

Status: measured on A8U4I5 (Michael's Mystique, 051A revision 02, 4 MiB),
boot 508, build `d1a6f262-dirty` driver and HAL (the Direct3D change,
[record](2026-10-19-mystique-direct3d.md)). Half-Life 1.1.1.0, 640x480x16
fullscreen, on an 800x600x16 desktop. Evidence in
[`docs/probe/a8u4i5-mystique-half-life-2026-10-19/`](../probe/a8u4i5-mystique-half-life-2026-10-19/).

## How it was run

`HL.EXE -console -condebug +timedemo mwd5`, launched through the agent
with `EngineType` 2 (OpenGL), then 3 (Direct3D), in HKCU
`Software\Valve\Half-Life\Settings`. The command-line `+timedemo` does not
run until the main menu's Console entry is clicked. Michael clicked it
both times, and on OpenGL typed `timedemo mwd5` twice more. That
explains why the 2026-10-18 scripted launch on the 2164W never produced a
score. Each score below was read from a console log taken after the
launch; the log before the launch held only the old 0-frame entry.

V9XTRACE snapshots were taken before OpenGL, after OpenGL (HL closed with
WCLOSE), and after the Direct3D timedemo with HL still running. Every
snapshot names HL.EXE in its process list, `BuildsAgree=1`.

## Scores (mwd5, 394 or 393 frames)

| Renderer | Run | Result |
|---|---|---|
| OpenGL | 1, cold | 65.330 s, **6.031 fps** |
| OpenGL | 2 | 61.850 s, 6.354 fps (Michael's photo of the console) |
| OpenGL | 3 | 60.577 s, 6.488 fps |
| Direct3D | 1, cold | 43.506 s, **9.056 fps** |
| Direct3D | 2 | 42.702 s, 9.203 fps (Michael's photo of the console) |
| Direct3D | 3 | 42.716 s, 9.200 fps |

## Where the drawing went

**OpenGL** (all three runs, snapshot difference): 149,851 render-interface
draws, 147,305 drawn on the engine (98.3 %). The ICD's `paths` line at
the end of run 1: `hw=51070/1307987` batches/triangles against `cpu=459/1684`
and `untextured=474/948`, `hw-refused=1/2`, `hwtex lost=0`.

The CPU batches cluster in the same 10-second windows as 94 failed
texture creates and 93 evictions (`hwtex create-failed=94`). That is texture
memory running out on this 4 MiB card. The 8 MiB 2164W did not run short
(placed 104, no make-failed).

**Direct3D**: 106,345 batches, 442 refused by the engine (0.4 %).
`NoHandleBlendOther=388` untextured blends, `MgaSetupRefusedSplit=1375`
triangles.

Both sessions: `MgaSetupRefusedSplit` 4,204 (OpenGL) and 1,375 (D3D)
triangles that the setup refuses and nothing draws, the open item from
the 2164W. `EngineIdleTimeouts`, `EngineFifoTimeouts` and
`RingSpaceTimeouts` all 0, so the chunked FIFO waits held through about
a quarter of a million batches on the 32-entry FIFO.

## Not established

- How the frames looked. Michael's photo shows the console; nobody judged
  the scenes, and the agent cannot capture flipped output.
- The Multiplayer menu (the 2164W's OpenGL corruption issue).
- The engine share of the two warm Direct3D runs: the snapshot was taken
  after run 1.

Gates: none; no code changed.
