# HD 4600, 3DMark 2000 default run: 1982 3D marks, no refused batch, and the per-batch cost a tester's machine showed

Date: 2026-10-20. Machine: the i5-4570 (`8086:0412`, SUBSYS `1028:05A4`),
boot 34 after an agent `reset`, the dev set `163b034a-dirty` (0.19.1,
driver and HAL agree). Agent 0.12.0 `dev-012c`. Evidence:
[`../probe/hsw-4570-3dmark2000-baseline-2026-10-20/`](../probe/hsw-4570-3dmark2000-baseline-2026-10-20/)
(`V9XSNA7` taken after the run with 3DMark still open, and `V9XBOOT`).

A baseline Michael asked for before work on the issues in
`docs/issues/2026-10-20-haswell-3dmark2001se-artifacts-field.md`
(report V9X-D9VZS6). One run, recorded, not compared.

## The run

3DMark2000 v1.1 (build 340, registered), installed from
`3DMark2000v11.exe` pushed by the agent. Run Default Benchmark: 1024x768,
16-bit colour, triple buffering, 16-bit Z, 16-bit textures. Desktop
800x600x16.

| Test | Result |
|---|---|
| 3DMark score | **1982** |
| CPU speed | 155 CPU 3D marks |
| Game 1 Helicopter, low / medium / high | 34.9 / 23.8 / 8.4 fps |
| Game 2 Adventure, low / medium / high | 45.1 / 31.2 / 21.9 fps |
| Fill rate, single / multi texturing | 189.1 / 189.0 MTexels/s |
| High polygon count, 1 / 4 / 8 lights | 906 / 967 / 961 KTriangles/s |
| Texture rendering, 8 / 16 / 32 / 64 MB | 60.1 / 59.9 / 57.2 / 18.1 fps |
| Bump mapping, all four | Not supported |

3DMark reports the HAL with 16384 KB of video memory, 15447 KB of it for
textures, and one texture blending stage.

## What the snapshot shows

One process (`3DMARK2000.EXE`, 5 contexts) in this boot.

- 0 engine FIFO or idle timeouts, 0 resets; the engine still valid.
- **0 batches refused** (`BatchesEngineRefused`), and every mapping
  reason 0. The tester's 4,863 refusals are not reproduced by 3DMark 2000
  on this machine; that report's counters cover 3DMark 2001 SE as well.
- 0 mip chains with a level out of place (`MipGapOffset`), against the
  tester's 46,938.
- 4,888 mip trees placed, **3,950 declined**, the last for
  `V9X_D3D_PLACE_ALLOC` (the heap had no room): the 16 MiB heap, as the
  issue's second section reads it. The 64 MB texture test at 18.1 fps
  against 57-60 for the others is consistent with it, not shown by it.
- 8,181 flips handled; 14.9M `Flip` calls returned still-drawing.

The per-batch cost, at 3.19 GHz:

| µs | this run | the tester's report |
|---|---|---|
| engine draw, per batch | 157.7 | about 158 |
| render-engine waits (two a batch) | 75.5 (37.8 each) | about 77 |
| vertex copy | 60.6 | about 59 |
| state write | 6.8 | |
| kernel build | 4.5 | |

1.52M batches, 43 triangles each, took 240 s of the 363 s between the
first D3D call and the dump. The two machines agree to within a few per
cent, so the tester's slow runs are this design's cost and not
something particular to that board.
