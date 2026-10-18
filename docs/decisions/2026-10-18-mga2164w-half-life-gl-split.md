# Half-Life OpenGL on the MGA-2164W: every draw goes to the CPU

2026-10-18, A8U4I5 (Matrox Millennium II, MGA-2164W), boot 496, driver
`50e7eeba-dirty` (the code of 4a23aea2). Step 6 of
[`../plans/matrox-mga2164w-render-interface.md`](../plans/matrox-mga2164w-render-interface.md),
for its decision 2. Evidence:
[`../probe/a8u4i5-mga2164w-render-interface-2026-10-18/`](../probe/a8u4i5-mga2164w-render-interface-2026-10-18/)
(`V9XSNA7-boot496-before-hl.ini`, `-after-hl.ini`, and the ICD's `paths` and `hwno` lines in `V9XGL-boot496-hl-paths.LOG`).

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

OpenGL sent 267,614 render-interface draws (`R3dDrawCalls`), and the
engine refused every one, so the software fallback drew the whole frame.
It drew, as Michael saw.

**Correction (the same day).** The first version of this record counted
133,572 refusals for "CPU texture levels" (`M64AcceptPolicy02`) as textures
the ICD kept on the CPU. They are not. The ICD's own log (`V9XGL.LOG`,
`paths ... hw-refused=133572/6068291`) shows those are its resends: a
batch whose surface texture the engine refused goes again as the CPU copy,
which the engine refuses as CPU levels before the fallback draws it. The
ICD's reasons for not making a surface (`hwno`) stayed in single figures:
it got surfaces for Half-Life's textures.

So each textured batch was counted twice. Taken once, the first attempts
were:

| First attempts | Batches | Share |
|---|---|---|
| Textured, surface texture (`hw-refused`) and untextured (470) | 134,042 | |
| Refused, surface texture the engine cannot read (`09`, TEXTURE) | 80,417 | 60 % |
| Refused, linear or mipmapped filter (`14`) | 42,789 | 32 % |
| Refused, blend (`04`) | 10,836 | 8 % |
| **Drawn on the engine** | **0** | 0 % |

The three refusals add up to the 134,042 first attempts.

Direct3D, which has no fallback:
- 1,089 batches refused for blend (`M64Policy04`);
- 833 triangles whose planes overflowed the setup (`M64Policy13`,
  `MgaSetupRefusedSplit`).

None of these were drawn. No idle timeouts.

## What this says about decision 2

Decision 2 asks whether explicit draws may take the Direct3D path's
approximations: nearest for linear, the stipple for source alpha. The map
checks the blend, then the texture, then the filter.
- The 32 % refused for filter had surface textures the engine can read.
  Nearest for linear would put them on the engine.
- The 8 % refused for blend are refused before their textures are looked
  at.
- The 60 % whose surfaces `v9x_d3d_mga_resolve_texture` will not sample
  come before either question, and they are the bigger lever. Candidates:
  ARGB4444, which the describe offers but the 2164W does not sample; a
  surface DirectDraw placed itself after `v9x_d3d_mga_create_surface`
  declined it, at a row tpitch cannot state; or a shape outside 1 to 256.

Boot 497 counts that reason (`MgaTexRefused*` in V9XTRACE, with the last
refused surface's bit count, alpha mask, size and pitch). The
approximations are not worth deciding until those textures reach the
engine.

## Not measured

- A timedemo score, OpenGL or Direct3D.
- Which texture formats and shapes Half-Life asked for (counted from
  boot 497).
