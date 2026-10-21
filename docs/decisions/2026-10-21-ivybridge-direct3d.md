# Ivy Bridge Direct3D on W9QI4E14: Haswell's engine with Gen7's words and IVB's PIPE_CONTROLs passes V9XD32

Date: 2026-10-21
Status: **measured once per configuration.** V9XD32 passes every check at
16 and 32 bpp, and with a 24-bit Z buffer. 3DMark 2000's default benchmark
runs to its score (1910). This is step 6 of
`docs\plans\intel-ivybridge-gen7.md`.

Captures: `docs\probe\w9qi4e14-ivb-hd4000-d3d-2026-10-21\`. Package
`build\win98se-intel-gma`, build `178201b9-dirty` (`BuildsAgree=1`).

## What changed

The Haswell render path (`hsw_3d.c`'s builders, `d3d_hsw.c`) now serves
Ivy Bridge. What differs is chosen by a chip setting in `hsw_3d.c`,
`v9x_intel_hsw_3d_set_chip`. `d3d_hsw.c`'s readiness check sets it from
`V9X_DD_ENGINE_CAP_GEN7_IVB` before the initialising batch. The value is
a property of the machine, so V9XHAL's shared statics hold it safely.

From Mesa genxml (gen7.xml against gen75.xml):

- **RENDER_SURFACE_STATE DW7** is 0. Its shader channel selects (27:16)
  are Gen7.5 only, and on Gen7 the word holds the clear colours and min
  LOD.
- **3DSTATE_PS DW4**: Maximum Number of Threads at 31:24 (31:23 on
  Gen7.5), and no Sample Mask field. 63 threads gives `3F000401h`, where
  Haswell writes `1F801401h`.

From crocus and i965, which quote the IVB PRM:

- After `PIPELINE_SELECT(3D)`: a CS-stall PIPE_CONTROL with a post-sync
  write, then a zero-vertex point-list `3DPRIMITIVE`.
- After `3DSTATE_PUSH_CONSTANT_ALLOC_PS`: a CS-stall PIPE_CONTROL ("must
  be programmed in the ring after this instruction").
- Before `3DSTATE_URB_VS` and before `3DSTATE_VS`: the VS workaround, a
  depth-stall PIPE_CONTROL with a post-sync write.
- **WaCsStallAtEveryFourthPipecontrol.** Every fourth PIPE_CONTROL
  without CS stall gets one, plus stall-at-scoreboard where no companion
  bit is set. It is counted per batch (`struct v9x_hsw_batch`).

The post-sync writes go to status dword 16
(`V9X_INTEL_HSW_STATUS_IVB_WA`), which nothing reads.

Not changed, and checked as the same in genxml: every other 3DSTATE
command the path emits, SAMPLER_STATE, BLEND_STATE, DEPTH_STENCIL_STATE,
the depth formats, the EU encodings and send messages, and the URB and
push-constant values, which fit Gen7's narrower fields.

Not added: i915's IVB L3 workarounds (L3CNTLREG1, the L3 chicken mode,
L3SQCREG4), whose BIOS values differ
(`2026-10-21-ivybridge-gt-forcewake.md`). V9XD32 passed without them. A
render fault that only shows under load is where to look first.

Host tests (`test_intel_hsw_3d.c`, `test_ivb_variant`), all worked by
hand:

- the setter's refusals;
- DW7 0 and PS DW4 `3F000401h`;
- the fourth-PIPE_CONTROL rule with and without a companion bit;
- the init batch's IVB prefix (select, CS-stall write to the workaround
  dword, the point list) and its two VS workarounds;
- its length, 198 dwords against Haswell's 172.

The chip is put back to Haswell, and Haswell's DW7 rechecked, before any
other test runs. With the post-allocation CS stall removed, the length
check fails.

The descriptor stamps `D3D` for Ivy Bridge (EngineCaps `00080017`) with
the Direct3D word `hardware-gen7`. The manifest agrees, and check-tree
compares the two.

## Boot 8 (Have Disk over boot 7's install, then `reset`)

`boot8-V9XHW.INI`: Direct3D `hardware-gen7`, EngineCaps `00080017`,
`Direct3DMode=hardware`.

| Run | Result |
|---|---|
| V9XD32 `/bpp16` (640x480x16, Z16) | **PASS**, Failures 0: T1 0 mismatches; T2 corners 7; T3 depth 7; T5 precision; T6 the four texels exact; T7 blend average `847D00` (the GM965's value); T4 off-screen 0 mismatches |
| V9XD32 (640x480x32, Z16) | **PASS**, Failures 0, T7 average `807F00` |
| V9XD32 `/z24` (640x480x32, 32-bit Z) | **PASS**, Failures 0: the clear wrote the 24-bit far value (`ZClearFar24`), the depth tests held (`ZDepthFar24`) |

The snapshot after the first two runs (`boot8-after-d32-V9XSNA5.INI`):

- `AsyncSubmits` 22, `SyncSubmits` 3, and the breadcrumb at 25 (`19h`):
  every draw went through the render ring;
- no breadcrumb, flip-wait or engine timeouts, no resets;
- 4 Direct3D contexts created and destroyed, 24 DrawPrimitives, 8
  DrawOnePrimitive, 2 textures.

The machine stayed on boot 8 throughout.

## 3DMark 2000 Pro, default benchmark (boot 8)

Installed from the owner's `C:\extras\Tools\3DMARK00.exe` into
`C:\Program Files\MadOnion.com\3DMark2000`, with the licence accepted and
the published Futuremark registration entered on Michael's instruction.
3DMark version 340.

The default benchmark ran to its score: 1024x768, 16-bit colour, triple
buffer, 16-bit Z, 16-bit textures, 58 Hz. The card is "Velocity9x Intel
HD Graphics 4000 (Ivy Bridge)", V9XHAL.DLL 0.23.00.0000, 64320 KB video
memory, one texture blending stage. Captures:
`docs\probe\w9qi4e14-ivb-hd4000-3dmark2000-2026-10-21\` (`score.png`,
`details-*.png`, `game1-helicopter-frame.png`).

| | |
|---|---|
| 3DMark result | **1910** (CPU speed 177) |
| Game 1 Helicopter, low / medium / high | 29.8 / 25.7 / 11.2 fps |
| Game 2 Adventure, low / medium / high | 39.6 / 30.0 / 22.9 fps |
| Fill rate, single / multi texturing | 111.3 / 111.6 Mtexels/s |
| High polygon count, 1 / 4 / 8 lights | 1251 / 1233 / 1225 Ktriangles/s |
| Texture rendering, 8 / 16 / 32 / 64 MB | 59.8 / 59.6 / 59.3 / 58.6 fps |
| Bump mapping | not supported (one texture stage) |

The score is recorded, not compared.

The one frame captured mid-run (`game1-helicopter-frame.png`, Game 1 at
1024x768) shows textured terrain, trees, fog, the helicopter and the HUD
drawn as expected. Screenshots are blind during flips, so that is one
frame, not an inspection of the run.

HAL snapshot afterwards (`after-3dmark2000-V9XSNA6.INI`, builds agree):

- `AsyncSubmits` 1,667,063 and `SyncSubmits` 7,439 on the render ring;
- no breadcrumb, flip-wait or engine timeouts, no resets;
- `FlipHandled` 7,820 and `FlipTakenAtDone` 7,819, none declined or
  abandoned, the last still pending at the snapshot;
- `DrawsIntoPresented` 0;
- `CountBltEngine` 12,992 of `CountBlt` 12,992;
- 64,483 DrawPrimitives and 1,632 textures created.

The machine was responsive afterwards, on boot 8. The relay server went
down during the install and the agent was out of reach for about 50
minutes; the machine itself did not restart.

So the IVB L3 workarounds were not needed for this run either.

## Not measured

- Another game or benchmark: 3DMark 99 and 2001 are on the machine as
  installers in `C:\extras\Tools`.
- OpenGL, Glide and 3DCIF through the render interface, which the
  Haswell path carries. None has been run on Ivy Bridge.
- Whether the L3 workarounds matter under a real load.
