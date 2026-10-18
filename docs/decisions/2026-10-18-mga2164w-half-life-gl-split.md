# Half-Life OpenGL on the MGA-2164W: every draw goes to the CPU

2026-10-18, A8U4I5 (Matrox Millennium II, MGA-2164W), boot 496, driver
`50e7eeba-dirty` (the code of 4a23aea2). Step 6 of
[`../plans/matrox-mga2164w-render-interface.md`](../plans/matrox-mga2164w-render-interface.md),
for its decision 2. Evidence:
[`../probe/a8u4i5-mga2164w-render-interface-2026-10-18/`](../probe/a8u4i5-mga2164w-render-interface-2026-10-18/)
(`V9XSNA7-boot496-before-hl.ini`, `-after-hl.ini`).

## What ran

Michael played Half-Life 1.1.1.0 in Direct3D and in OpenGL on the
800x600x16 desktop and reported both working. A scripted
`-gl ... +timedemo mwd5` launch of mine also ran for about ten seconds
before WCLOSE ended it: it had read the previous session's console log as
the result. So there is **no score**. The console log after the sessions
holds only a timedemo of 0 frames.

The counters below are the difference between two V9XTRACE snapshots of
boot 496. One was taken just before my launch, the other after Michael
quit. They cover all three HL.EXE sessions: `d3d-contexts=2`,
`gl-describes=1`.

## The split

OpenGL, through the render interface:

| | Draws | Share |
|---|---|---|
| Render-interface draws (`R3dDrawCalls`) | 267,614 | |
| Refused, CPU texture levels (`M64AcceptPolicy02`) | 133,572 | 50 % |
| Refused, surface texture the engine cannot read (`09`, TEXTURE) | 80,417 | 30 % |
| Refused, linear or mipmapped filter (`14`) | 42,789 | 16 % |
| Refused, blend (`04`) | 10,836 | 4 % |
| **Drawn on the engine** | **0** | 0 % |

The four refusals add up to every draw. All of Half-Life's OpenGL frame
was drawn by the software fallback. It drew, as Michael saw.

Direct3D, which has no fallback:
- 1,089 batches refused for blend (`M64Policy04`);
- 833 triangles whose planes overflowed the setup (`M64Policy13`,
  `MgaSetupRefusedSplit`).

None of these were drawn. No idle timeouts.

## What this says about decision 2

Decision 2 asks whether explicit draws may take the Direct3D path's
approximations: nearest for linear, the stipple for source alpha. On this
title, allowing both would move at most the 20 % refused for filter and
blend.
- The map checks the texture before the filter, so the 16 % refused for
  filter had textures the engine can read. Nearest for linear would put
  them on the engine.
- The blend check comes first of all, so the 4 % refused for blend may
  also carry textures the engine cannot read.

The other 80 % are refused before either question arises, and they are
the bigger levers:

- **Textures (`09`, 30 %).** The ICD placed a surface texture that
  `v9x_d3d_mga_resolve_texture` would not accept. Candidates: ARGB4444,
  which the 2164W does not sample, a row pitch it cannot state, or a
  shape outside 1 to 256. Which one is not counted.
- **CPU levels (`02`, 50 %).** The ICD kept these textures on the CPU,
  for example ones it could not place in video memory or in a format the
  describe does not offer for surfaces. Why is not counted either.

The first step is a counter or a probe that names those two reasons. The
approximations are not worth deciding until the textures reach the
engine.

## Not measured

- A timedemo score, OpenGL or Direct3D.
- Which texture formats and shapes Half-Life asked for.
