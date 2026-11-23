# DDHELP faults in the HAL: a DestroySurface on its thread runs a GCN draw that holds another process's texture pointer

Date: 2026-11-23. Status: closed the same day (below); 1.0 blocker B8 (two Direct3D processes on
the HAL's shared statics). Seen once, then fixed and the case rerun.
Machine: W9QI0DAD, Radeon Oland `1002:6610`, Windows 98 SE, DirectX 9.0c,
1024x768x32, boot 129 on `a92e01f3-dirty`'s ati set (the B2 and B11 build
over 0.61.0's tree).

## What was run

The UT2004 demo (build 3334, Direct3D 8) windowed at 640x480 (its ini's
`StartupFullscreen` set False for the run and restored after), a
`dm-rankin` botmatch, and, 40 s into it, the release probes one after
another: V9XWPRS, then V9XGLP, then V9XR3DP. Runner:
`run-b8.ps1` (session scratchpad), evidence in
[`../probe/1.0-b8-oland-2026-11-23/`](../probe/1.0-b8-oland-2026-11-23/).

## What happened

1. **V9XWPRS beside UT2004:** its OpenGL half passed (40 swaps, every
   point read green) at 139 ms a swap, against 0.74 ms alone on the same
   build; its DirectDraw half failed after 5 presents
   (`DDrawResult=FAIL-PRESENT`, 27.9 ms average). Alone, both pass.
2. **V9XGLP started; then DDHELP faulted:** "DDHELP caused an invalid
   page fault in module V9XHAL.DLL at 0177:b043ec39", `EDI=0A3CDFB0`,
   bytes `8b 5f 04` (`mov ebx,[edi+4]`). The UT2004 window showed the
   same frame from then on; V9XGLP did not finish.
3. **The HAL's own capture** (`V9XTRACE.INI`, `FaultFlush=1`, build
   `a92e01f3-dirty`, `FaultAddress=0xB043EC39`): the last entry is
   `DestroySurface enter 0x8B2CF0C4`, after a run of Blt, Lock and Unlock
   calls. No engine timeout or reset was counted.
4. Closing the fault dialog left the screen unreadable (the agent's
   screenshot shows the frame buffer read at the wrong layout); the
   machine was restarted by `reset` (boot 130, clean desktop).

## Where it faulted

`0xB043EC39` is `d3d_vi.obj` + 7FBh (the installed HAL's sections match
the build tree's map; the bytes at that address are the dialog's): in
`v9x_d3d_vi_resolve_texture` (`src\display32\d3d\d3d_vi.c`), the
`surface->lpGbl` read through `sampler->object`, the bound texture's
surface pointer. `0A3CDFB0` is below 2 GiB, in a process's private arena:
DDHELP could not read it.

So DDHELP, cleaning up after a process (the surface being destroyed is
`8B2CF0C4`), entered the HAL's DestroySurface, and that call ran a GCN
draw whose texture was another process's private pointer, held in the
HAL's shared statics. Which process's draw it was, and which path inside
DestroySurface ran it, were not read; UT2004 is the likely owner (the
only Direct3D program drawing textures then), not established.

This is the hazard the B8 record names
([issue](2026-10-06-hal-statics-shared-across-processes.md)): statics
holding per-process pointers, used from a process that is not their
owner.

## Next

1. Read which path in `ddhal_core.c`'s DestroySurface reaches a draw on
   the GCN engine (a pending batch flushed before the surface is freed is
   the first suspect), and what state it draws with.
2. A draw that holds another process's pointers is never resolved from a
   process that is not its owner: drop or defer it there, or resolve
   texture pointers into offsets when the draw is queued. Then the same
   run again; and V9XWPRS's DirectDraw failure beside UT2004 explained.
3. The same run on the HD 4600 (Intel), the plan's other B8 machine.

## Fixed, 2026-11-23

`v9x_d3d_dp2_flush_pending` draws the pending run only in the process
whose context queued it, as `v9x_d3d_tnl_pending` already did for the T&L
batch; elsewhere it discards the run and counts it
(`Dp2ForeignDropped`). The same case again, on the fixed HAL: no DDHELP
fault, V9XWPRS and V9XGLP pass and V9XR3DP completes beside UT2004 on the
Oland (98 runs dropped) and the HD 4600 (111); no engine timeout
([record](../decisions/2026-11-23-1.0-b8-dp2-run-owner.md)). The owner
loses the dropped triangles; how many reach the screen was not measured.
Status: closed.
