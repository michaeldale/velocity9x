# Netbook: NFS II SE frame rate on GLIDE2X.DLL, 2026-10-09

- Machine: MICHAEL-NETBOOK (GMA 950, Gen3), Velocity9x 0.13.0 set,
  `MaxPhysPage=40000`, boot 129, agent 10.0.1.248.
- Record: [Glide frame rate on the netbook](../../decisions/2026-10-09-glide-frame-rate-netbook.md).
- Plan: [glide-2x-wrapper.md](../../plans/glide-2x-wrapper.md), Phase 4.

Every run used the same drive: launch, Escape past the intro, Down four
times 1.5 s apart, Enter, then race in attract mode. The DLL logs a
profile every 15 s (rdtsc, in millions of cycles, at about 1.6 GHz).

| File | What it is |
|---|---|
| `B129-SUMMARIES.TXT` | The periodic profile lines of runs 1-11, one block per run, cut from logs too large to keep |
| `B129-11-TRIPLE-BUFFER.LOG` | The full log of run 11, the state committed |
| `B129-11-TRIPLE-BUFFER-RACE.png` | Run 11 in the race |
| `B129-12-PAUSE.png` | Run 12, the run 11 DLL: the pause dialog after Escape held 400 ms (a tap is missed in the race) |
| `B129-12-PAUSE-LATER.png` | Eight seconds later: the agent's screenshot caught a frame mid-draw while the dialog kept swapping |
| `B129-12-PAUSE-CALLS.TXT` | Run 12's log from the race's end, without per-draw lines: `grRenderBuffer(FRONT)` on Escape, then the dialog drawn in the back buffer inside a 64,48-576,432 clip window and swapped |

| Run | Change | Swaps per 15 s in the race | Flicker (operator) |
|---|---|---|---|
| 1 | Profiled; clear through the interface (CPU) | 81 | not reported |
| 2 | Clear by blitter colour and depth fills | 173 | yes |
| 3 | Plus the CPU fixes: fog table, clip fast path, word compare | 269 | yes |
| 4 | Plus batch-end counts | 271 | yes |
| 5 | Fills after a finish | 271 | yes |
| 6 | Colour fill only, depth by CPU | 222 | yes |
| 7 | No fills (baseline repeat) | 97 | in game none, small in menu |
| 8 | Depth fill only | 101 | yes |
| 9 | Clear drawn as a quad, two buffers | 262-270 | yes |
| 10 | Run 9 plus finish and wait for retrace after each flip | 224 | none |
| 11 | Run 9 with three buffers | 278 | very small in menu, none in game |
