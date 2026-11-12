# HD 4600, 3DMark 2001 SE: Car Chase with DXT textures hangs the render engine, then DirectDraw faults

Date: 2026-11-12 to 2026-11-13. Seen on the i5-4570 (HD 4600, W9QI5A83).
Builds:
- boots 218 to 220 and 224: main's intel-gma set at `746c0a42`, installed
  by `run-release-checks.ps1`;
- boots 221 and 222: two HAL-only measurement swaps (below).

3DMark 2001 SE build 330 at 1024x768, 32-bit colour, Z24 and software
T&L. Status: **open**. The cause is not found.

## What happens

The full benchmark with 3DMark's texture format at "Compressed" never
reaches a score. In Car Chase, the first test, the sequence is this:

1. **Two render-engine idle timeouts** (`EngineIdleTimeouts` 0 to 2).
   `d3d_hsw.c` then quarantines the render engine for the process
   (`v9x_d3d_hsw_quarantine`). That clears `V9X_DD_ENGINE_VALID`, so
   `ready()` fails from then on.
2. **Every later DrawPrimitives2 call is refused** at the core's entry
   check (`Dp2RefusedBuffers`): 5,537, 5,555, 5,577 and 5,586 in four
   runs, of 6,200 to 6,240 calls each. No `dx8-buffer` or
   `unknown-handle` line is logged, so none came from buffer resolution.
3. **DirectDraw faults**, 32 to 58 s into the run. It is an invalid page
   fault in `DDRAW.DLL` at `0177:BAAD33A0` (module base `BAAA0000`), with
   EAX=0 and EDI=0. The code bytes at EIP, `f6 00 01`, test [EAX], a null
   pointer. The HAL's fault watch records the same address
   (`WatchFault*`).
4. **Until a restart**, every later 3DMark start faults at the same EIP
   before its main window opens: two such starts. One other start
   faulted in `KERNEL32.DLL` at `BFF7A4B2`, with no new HAL attach.
   Pointers in the `8301xxxx` shared arena appear in both faults. A
   `reset` clears this.

The fault-flush record of the timeout (`V9XTRACE.INI`, with the render
ring head) was each time overwritten by the DirectDraw fault's record,
so the hung batch has not been located.

## Narrowed

Car Chase alone, each on a fresh boot unless noted:

| Boot | HAL | Texture format | Result |
|---|---|---|---|
| 220 | main (`746c0a42` code) | Compressed | 2 timeouts, 5,577 refused, DDRAW fault at 36 s |
| 221 | `4146a0f0`, the build before the dragon-stage commit (hash checked) | Compressed | same: 2 timeouts, 5,586 refused, same fault |
| 222 | `4146a0f0` without the `DDCAPS2_WIDESURFACES` claim (measurement, not in the tree) | Compressed | no timeout, no refusal; 3DMark stops before drawing: "FATAL ERROR: failed to validate material" |
| 223 (same boot after a restore) | main | 32 bit | completes: 0 timeouts, 0 refused, 15,740,176 triangles |
| 223 (same boot) | main | DXT1 | 2 timeouts, about 13,200 refused, DDRAW fault at 35 s |

So:
- **The hang comes with DXT textures, DXT1 alone included.** It does
  not need DXT3 or DXT5.
- **It predates `746c0a42`.** The two-stage, ADD, projection and
  stage-2 fold changes are not the cause.
- **The wide-surfaces claim (`27d4f307`) is not tested by this.**
  Without it Car Chase fails material validation and never draws, so
  boot 222 says nothing about the hang.
- **Boot 146's full run (2026-10-30, score 1834)** predates the HAL's DXT
  support (boot 157). With no DXT format offered, "Compressed" then gave
  uncompressed textures.
- **V9XD32's T19 Dxt1/Dxt3 checks pass**, so this is something
  particular to the textures Car Chase uses.

## Hypotheses

- **Killed: a declined DXT chain is sampled in the engine's layout.**
  Mip trees are declined for room (reason 4: 1 before the boot-220 hang,
  1,687 in the 32-bit run, which did not hang). A declined chain is
  placed by DirectDraw, and `v9x_d3d_hsw_texture` refuses a BC format
  whose `dwBlockSizeX` is set. `dwBlockSizeX` shares its storage with
  `lpVidMemHeap`, which every DirectDraw heap placement sets, so such a
  texture is refused, not sampled. That is a reading of the code, not
  a measurement.
- **Open:** a BC tree whose small levels (under 4x4 texels) or
  non-square shape the sampler reads past its block. Car Chase's DXT
  texture set has not been looked at.

## Score with 32-bit textures

Boot 224, all 21 tests, texture format 32 bit, census armed: **1743 3D
marks**. 0 engine timeouts or resets, 0 refusals of any kind, no fault,
141,636 DP2 calls, 243,649,310 triangles. Recorded, not compared.

| Test | Result |
|---|---|
| Car Chase, low / high | 16.8 / 5.2 fps |
| Dragothic, low / high | 41.1 / 2.7 fps |
| Lobby, low / high | 45.3 / 25.0 fps |
| Nature | 2.7 fps |
| Fill rate, single / multi | 176.4 / 606.0 MTexels/s |
| High polygon count, 1 / 8 lights | 4.0 / 3.4 MTriangles/s |
| Environment bump mapping | 58.4 fps |
| DOT3 bump mapping | not supported by hardware |
| Vertex shader | 26.1 fps |
| Pixel shader / advanced pixel shader | 59.0 / 11.9 fps |
| Point sprites | 1.8 MSprites/s |

## Next

- **Catch the timeout's record before the DirectDraw fault overwrites
  it.** Run `wait-file` on `C:\V9XDIAG\V9XTRACE.INI` matching
  `FaultCode=0x48535733`, then `get` it at once. That gives the render
  ring head at the timeout.
- **Name the textures Car Chase binds as DXT.** Record their sizes,
  levels and BC format: the d3d8 proxy logs resource creation. Then
  reproduce the hang with one such texture in V9XD32's T19.
- Until the cause is found, 3DMark on the 4570 is run with 32-bit
  textures.
