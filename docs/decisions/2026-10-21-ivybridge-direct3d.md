# Ivy Bridge Direct3D on W9QI4E14: Haswell's engine with Gen7's words and IVB's PIPE_CONTROLs passes V9XD32

Date: 2026-10-21
Status: **measured once per configuration.** V9XD32 passes every check at
16 and 32 bpp, and with a 24-bit Z buffer. This is step 6 of
`docs\plans\intel-ivybridge-gen7.md`. No game has run yet.

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

## Not measured

- A game or benchmark: 3DMark 99/2000/2001 are on the machine as
  installers in `C:\extras\Tools`.
- OpenGL, Glide and 3DCIF through the render interface, which the
  Haswell path carries. None has been run on Ivy Bridge.
- Whether the L3 workarounds matter under a real load.
