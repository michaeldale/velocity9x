# HD 4600, 3DMark 2001 SE: Car Chase with DXT textures hangs the render engine, then DirectDraw faults

Date: 2026-11-12 to 2026-11-13. Seen on the i5-4570 (HD 4600, W9QI5A83).
Builds:
- boots 218 to 220 and 224: main's intel-gma set at `746c0a42`, installed
  by `run-release-checks.ps1`;
- boots 221 and 222: two HAL-only measurement swaps (below).

3DMark 2001 SE build 330 at 1024x768, 32-bit colour, Z24 and software
T&L. Status: **fixed in source 2026-11-13**. A DXT or signed tree
that finds no room is now answered out of video memory, not left to
DirectDraw (section at the end). Measured on the 4570; how
DirectDraw's placement hung the engine is not established.

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

## 2026-11-13: fixed - a DXT tree with no room is out of video memory

A d3d8 proxy run (boot 224) listed Car Chase's textures. The compressed
ones are all DXT1, managed, with full chains to 1x1. Many are
non-square: 1024x512, 512x1024, 1024x256, 512x256, 512x128, 128x512 and
64x128.

Measurement HALs, each a HAL-only swap on a fresh boot (hash checked),
Car Chase with Compressed textures:

| Boot | HAL | Result |
|---|---|---|
| 225 | non-square BC trees declined | completes, 0 timeouts |
| 226 | BC surfaces with VALIGN_4 (Mesa's ISL setting; ours was VALIGN_2) | hangs as before |
| 227 | wide BC trees declined | hangs as before |
| 228 | tall BC trees declined | "failed to validate material", nothing drawn |
| 229 | the fix below | completes, 0 timeouts, 0 refused, textured frames |

**Killed:**
- **VALIGN.** The HAL lays BC levels out at j = 4 but states VALIGN_2,
  where Mesa states VALIGN_4. With VALIGN_4 Car Chase hung the same. The
  change was reverted: it fixed nothing, and what it does to the
  smallest levels is unmeasured.
- **Shape alone.** Declining only wide trees still hung, and declining
  only tall ones broke material validation. Declining every non-square
  tree changed which trees ever ran out of room.

**What the hanging runs had in common:** a BC tree declined for room.
The boot-220 run had exactly 1 decline (reason 4, `V9X_D3D_PLACE_ALLOC`)
before the hang. A declined tree went back to DirectDraw as NOTHANDLED,
and DirectDraw placed the DXT surface in its own heap. A DXT surface has
no bit count, so DirectDraw cannot size it right. Gen3 met the same with
its signed formats (`v9x_d3d_i9xx_unplaced`, the netbook, boot 304) and
answers such a texture out of video memory. Haswell never did.

How exactly a DirectDraw-placed DXT surface hung the render engine is
not established. This HAL already refused to sample one. The mechanism
is open; the trigger and the cure are measured.

**The fix:** `v9x_d3d_hsw_unplaced_out_of_memory` (`d3d_hsw_map.c`,
`test_unplaced` written first and seen failing). A tree with no room
whose format is not RGB (DXT or signed) is answered
`DDERR_OUTOFVIDEOMEMORY` from `v9x_d3d_hsw_create_miptree`. The Direct3D
8 runtime then evicts managed textures and tries again. RGB trees go to
DirectDraw as before.

Boot 229, Car Chase alone, Compressed: score window at 68 s, 0 engine
timeouts, 0 DP2 refusals, no fault, 24,745,318 triangles. 1,564 trees
were placed and 226 declined for room, each answered out of video memory.
The grabs show the road, sky, terrain and trucks textured.

**Full benchmark with the fix**, boot 229, all 21 tests, Compressed
textures: **2519 3D marks**. 0 engine timeouts or resets, 0 refusals of
any kind, no fault, 178,767 DP2 calls, 355,708,738 triangles. Recorded,
not compared.

| Test | Result |
|---|---|
| Car Chase, low / high | 27.0 / 6.1 fps |
| Dragothic, low / high | 40.0 / 22.5 fps |
| Lobby, low / high | 44.8 / 24.6 fps |
| Nature | 16.8 fps |
| Fill rate, single / multi | 176.4 / 602.4 MTexels/s |
| High polygon count, 1 / 8 lights | 3.8 / 3.3 MTriangles/s |
| Environment bump mapping | 57.9 fps |
| DOT3 bump mapping | not supported by hardware |
| Vertex shader | 24.9 fps |
| Pixel shader / advanced pixel shader | 58.7 / 26.4 fps |
| Point sprites | 1.7 MSprites/s |

Gates: `build-host.ps1` (`test_unplaced` failing first, then passing)
and `run-checks.ps1` pass. The 4570 ran this HAL alone over the boot-218
intel-gma set.

Gen3 already had this answer (`v9x_d3d_i9xx_unplaced`) and is not
changed. Not checked: Ivy Bridge, which shares `d3d_hsw.c` and so takes
the same answer, unmeasured there.
