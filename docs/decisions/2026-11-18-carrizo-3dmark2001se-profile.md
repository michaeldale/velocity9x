# Carrizo R7E: where 3DMark 2001 SE's time goes on 0.54.0 (V9XPROF)

Date: 2026-11-18. Machine: M-XP7053 (AMD GX-420GI, Radeon R7E
`1002:9874`, GCN 3), boot 72, the released 0.54.0 `ati` files. Evidence:
[`../probe/carrizo-0.54.0-m-xp7053-2026-11-17/prof-3dmark2001se/`](../probe/carrizo-0.54.0-m-xp7053-2026-11-17/prof-3dmark2001se/).

3DMark 2001 SE scored 724 here
([record](2026-11-17-carrizo-0.54.0-modulate-alpha-and-ring.md)). That
was recorded, not compared, but it is a third of the Oland's on a faster
CPU. Michael expected the CPU and the driver to be where the time goes.
This measures it.

## How

- `V9XPROF.EXE` (oland worktree build, `ffd358c9-dirty`) started 3DMark
  2001 SE and sampled its main thread about once a millisecond. The
  default benchmark ran to its score (5.8 min). 3DMark was closed so the
  profile was written: 377,565 samples, none lost.
- **The map.** The HAL's map is from the release commit, rebuilt in a
  throwaway worktree (`f93b15e3`). Its `V9XHAL.DLL` differs from the
  shipped one in 10 bytes (timestamps), so the map's addresses are the
  shipped DLL's.
- **Statics.** The map has public symbols only, so
  `scripts/report-profile.ps1` puts a static function under the public
  before it. That put 16 % of the driver's samples under
  `si_dma!v9x_si_dma_plan_row_`, code the Carrizo never runs. Each hot
  object was disassembled with `wdis`. An object's base is a public
  symbol's map address minus its offset in the object, and samples were
  then attributed to exact functions, statics included (scratchpad
  scripts `proffunc.py`, `profhot.py`; output in `hal-functions.txt`).

## By module (all samples)

| Module | Share |
|---|---|
| V9XHAL.DLL (the driver) | 48.8 % |
| KERNEL32.DLL | 19.1 % |
| D3D8.DLL (the runtime, software T&L) | 9.8 % |
| 3DMark2001SE.EXE | 9.1 % |
| MSVCRT, 3DMark's RLMFC, E2MFC, GRPHMFC, WINMM | 11.4 % |
| everything else | under 2 % |

## Inside the driver (share of all samples)

| What | Functions | Share |
|---|---|---|
| CPU waiting for the GPU to go idle | `vi_cp!v9x_vi_cp_poll_fence` | 7.1 % |
| copying the command stream into the ring, a dword at a time | `vi_cp!v9x_vi_cp_put`, its copy loop at +155h | 6.9 % (7.5 % with the rest of `put`) |
| copying each batch's vertices into the GPU's vertex arena, a dword at a time | `d3d_vi!v9x_d3d_vi_draw`, its loop at F1Dh-F39h | 7.3 % (7.6 % with the rest) |
| polling the display controller around flips | `eg_scanout!v9x_eg_scanout_live`, `eg_flip_pending`, `eg_in_vblank` | 6.0 % |
| the core's CPU vertex work: DP2 walk and FVF conversion, batching records, culling, clipping, fog table | `r3d_records`, `d3d_dp2`, `d3d_core` DP2 paths, `r3d_cull`, `r3d_clip`, `d3d_fog` | about 10 % |
| a per-surface texture forget walk | `d3d_core!v9x_d3d_textures_forget_surface` | 2.0 % |
| ring kicks, PM4 building, the rest | `vi_cp_kick`, `vi_3d`, other | the remainder |

The driver's three largest costs are not computation:
- the CPU waiting for the GPU;
- the CPU copying dwords into the GPU's window;
- the CPU polling the display.

Together they are about 27 % of the run. Filed:
- [the CPU waits for the GPU at flips, locks and DMA fills](../issues/2026-11-18-vi-cp-full-drain-serialises-cpu-and-gpu.md);
- [ring and vertex copies are dword loops into the GPU window](../issues/2026-11-18-vi-dword-copies-into-gpu-window.md);
- [flip waits spin on the display controller](../issues/2026-11-18-carrizo-flip-waits-poll-scanout.md).

## KERNEL32: 19.1 %, not resolved

Two places hold 80 % of KERNEL32's samples. Read against the guest's own
`KERNEL32.DLL` export table:
- offset 2ED80h, 52 %: 14 bytes before the ordinal-only export #74;
- offset 29B30h, 28 %: inside the range that follows `CancelWaitableTimer`.

Neither is a named function. One hypothesis, not tested: DirectDraw holds
the Win16 lock around every HAL callback
([measured](2026-09-26-98se-directdraw-holds-the-win16-mutex-around-every-hal-callback-measured.md)),
and KERNEL32's lock enter and leave would land in unexported code like
this. A V9XPROF run of a program that calls the HAL rarely would separate
it.

## What this does not say

- One run, one machine, one benchmark.
- V9XPROF samples the main thread only, so GPU-side time does not
  appear except as the CPU waiting for it.
- The copies' cost per byte depends on how this APU reaches its
  carve-out through the GPU's window. That was not measured on its own.
- How often each drain happens (flips, locks, DMA fills) was not
  counted, only the total time in the fence poll.
