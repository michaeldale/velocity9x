# Direct3D on the HD 3000: V9XD32 passes on the Gen6 builders

Date: 2026-11-20
Status: **measured once per configuration, M-NZHWBR boot 64.** Steps 5
and 6 of `docs\plans\intel-sandybridge-direct3d.md`. Build
`dc48a9b4-dirty` (branch `intel-snb`). Evidence:
`docs\probe\m-nzhwbr-snb-hd3000-d3d-2026-11-20\`, files `boot64-*`.

## What the build does

- `CAP_GEN6_SNB` selects a new engine, `V9X_D3D_SELECT_GEN6`
  (host-tested in `test_d3d_select.c`), whose ops table is the Haswell and
  Ivy Bridge entry points with Sandy Bridge's limits: Ivy Bridge's (one
  texture stage, no modulate2x, no bump formats) with DXT off, since
  Gen6's BC surfaces are not built. The device is named "Velocity9x HD
  3000".
- In `d3d_hsw.c` five wrappers choose the builders by the engine bit at
  each use: render and texture surfaces (`v9x_intel_snb_surface_2d`, 6
  dwords, in the Gen7 slots), mip counts (`v9x_intel_snb_surface_mips`,
  DW2 5:2, host-tested), samplers (`v9x_intel_snb_sampler`, its 12-dword
  border colour inside the 64 zeroed bytes at the border slot), the pixel
  kernel (`v9x_intel_snb_ps_kernel_build`, also in the fit check) and
  the draw batch (`v9x_intel_snb_draw_workload`). The T&L and pixel
  shader 1.1 paths stay Gen7's; neither is reachable without T&L or
  DDI 8, which Sandy Bridge does not offer.
- `ready()` no longer declines: the step 4 gate is gone.
- The i915 render-ring workaround registers are still not written.

## Result

`V9XD32.EXE`, the release checks' four configurations for this machine:

| Run | Failures | T1 | T4 | T13 | T14 | T15 | T16 | Result |
|---|---|---|---|---|---|---|---|---|
| default (32 bpp, Z16) | 0 | 0 mismatches, `0013579B` | 0 | PASS | PASS | PASS | PASS | PASS |
| `/bpp16` (Z16) | 0 | 0 | 0 | PASS | PASS | PASS | PASS | PASS |
| `/ddi6` | 0 | 0 | 0 | PASS | PASS | PASS | PASS | PASS |
| `/ddi5` | 0 | 0 | 0 | PASS | PASS | PASS | PASS | PASS |

T12 is skipped (one stage), T18 finds no T&L device and T19 no pixel
shaders, as on the HD 4000.

HAL counters after all four (`boot64-after-all-V9XSNAP.INI`):
`AsyncSubmits` 4523, `SyncSubmits` 5, `EngineIdleTimeouts` 0,
`EngineResets` 0. `BatchesEngineRefused` 12, three a run; this machine's
HD 4000 snapshots show 9 after the three V9XD32 runs of its release
checks (`docs\probe\hd4000-nzhwbr-ivb-2026-11-19\`), so these are
V9XD32's own refused cases.

So on Gen6 hardware, as built and host-tested: the 20-dword SF and
9-dword WM dispatch the pixel kernel with the payload where the PRM put
it; the PLNs read the setup data; the sampler message from m1-m2 and the
header-less render-target write from m1-m4 both work; depth testing with
a Y-tiled D16 buffer works; texturing with the Gen6 sampler and surface
works; asynchronous submission works, without the i915 workaround
registers.

## Not measured

- Z24 (D24_UNORM_X8): V9XD32 asked for Z16 in every run.
- Games: 3DMark 2000, OpenGL (Quake 2 and 3), Glide, run windowed.
- Mipmapped textures in a real program, and the kernel-fit declines a
  longer Gen6 kernel may cause.
- The picture on the monitor.
