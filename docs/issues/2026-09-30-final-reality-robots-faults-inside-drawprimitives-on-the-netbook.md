# Final Reality's Robots test kills the process inside DrawPrimitives on the netbook

Priority: high. A shipping-path benchmark that ran on this machine on
2026-09-20 now takes its process down on the fifth DrawPrimitives call,
twice out of two attempts, on the build the record-merging work is about to
commit.

Date: 2026-09-30. Machine: MICHAEL-NETBOOK, 945GSE / GMA 950 `8086:27AE`
revision 03, Windows 98 SE, boot 71, 1024x576x16 desktop. Installed HAL:
`C:\V9XREMOTE\JOBS\DPMERGE\HALNEW.DLL` (161,280 bytes), the working tree at
`b182f94` plus the uncommitted record-replay change in `d3d_core.c`,
`r3d_records.c` and `r3d_records.h`. V9XHW.INI: `Direct3D=hardware-gen3`,
`EngineStatusEnable=d3d-claimed`. Evidence:
[`../probe/final-reality-robots-2026-09-30/`](../probe/final-reality-robots-2026-09-30/).

## Reproduction

Final Reality 1.01, `C:\Program Files\Final Reality\FR.exe`, Advanced
options, rendering platform "Direct3D On-board Accelerator", Robots test.
Both runs were driven at the keyboard by the operator; the second FR
instance was launched through the agent, which showed only that the
executable starts and reaches its licence dialog.

- Run one, about 21:27: the process died. By the time it was looked at the
  crash dialog had been dismissed; Dr Watson was not running and left
  nothing.
- Run two, about 21:36: same. `V9XTRACE.EXE` was run straight after, before
  anything else touched Direct3D, and its ring holds the fatal call.

## What the driver recorded

Snapshot two (`netbook-boot71-after-second-crash-V9XSNA7.INI`), the tail of
the event ring for the second instance, PID `0xFFE3BD1B`:

```
874 D3dTargetLayout enter 0x05001002      640x480 target, pitch 1280
875 D3dContextCreate exit 0
876-877 Blt                               ok
878-879 D3dDrawPrimitives                 enter, exit ok
880-887 Blt x4                            ok
888-889 D3dDrawPrimitives                 enter, exit ok
890-895 CanCreateSurface, CreateSurface, D3dTextureCreate   ok
896-899 Lock, Unlock                      ok  (texture upload)
900-901 D3dDrawPrimitives                 enter, exit ok
902-903 D3dDrawOnePrimitive               enter, exit ok (fan declined, as designed)
904 D3dDrawPrimitives enter 0x94C00E80    NO EXIT
905-912 DestroySurface x3, FlipToGDISurface
913-914 D3dContextDestroyAll              DirectDraw tearing down the dead process
```

Every other enter in the boot has its exit. Event 904 does not, and the next
events are DirectDraw's cleanup after the process was gone. The fault is on
the CPU side, inside the HAL's DrawPrimitives handler or something it calls.

Counters, both snapshots, whole boot:

| Counter | after run one | after run two |
|---|---|---|
| D3dContextCreates / Destroys / DestroyAlls | 6 / 5 / 1 | 10 / 8 / 2 |
| D3dRenderPrimitiveCalls | 5 | 10 |
| DpRecords / DpRecordTriangles / DpRecordRuns | 1 / 2 / 1 | 2 / 4 / 2 |
| DpRecordRunsNoopJoined | 1 | 2 |
| D3dTextureCreates / TexturePlaced | 1 / 1 | 2 / 2 |
| Dp*Refused*, BatchesEngineRefused, i9xx refusals, engine timeouts, resets, breadcrumb abandons | 0 | 0 |
| OnePrimRefusedPrimType | 1 | 2 |
| Win16D3dDrawPrimsDepthMax, Win16LockDepthMax | 2, 2 | 2, 2 |

The two runs are identical in every delta: five DrawPrimitives calls, one
counted record of two triangles, one texture, one DestroyAll. It reproduces.

Two things the counters say that the ring cannot:

- **No engine refusal of any kind was counted.** `i9xx_draws_refused` is
  zero, so the new reason-6 replay path in `v9x_d3d_records_batch` was never
  entered. Whatever faults is upstream of, or independent of, the replay.
- **One record was counted across five calls.** `dp_records` increments
  after a record's state pairs are applied and its shape accepted, before
  the append. Either four of the five calls carried only state records or
  the terminator, or the fatal call faulted before its first record was
  counted. The snapshot cannot tell these apart; a per-call record count
  would.

## Review of the uncommitted change (2026-09-30, no fault found)

Read against the fault, not for style:

- `record_ends[64]` is bounded by construction: every entry consumes at
  least one of the 64 triangles of capacity, and every path that could
  reach `pending == capacity` flushes first, which resets `record_count`.
  A 63-record run followed by a one-triangle fan writes entry 63 and no
  further; the next append of any size flushes.
- Replay is not recursive: `flush` calls the sink directly for each record,
  and the sink's `-1` from a replayed record is discarded.
- The state-change flush saves and restores the whole `V9X_D3D_CONTEXT`
  around the old run. No engine writes to the context (grep of
  `d3d_i9xx.c` for context stores finds none), so the restore cannot
  clobber engine state advanced by the flush.
- The regression window is wider than the diff. Final Reality last ran on
  this machine on 2026-09-20 (intel94/95). Since then the whole
  record-merging series landed: `5f1ab1c`, `5da6648`, `c3c07d7`,
  `a6f89cb`, `c52281d`, `b182f94`, plus the working tree. Half-Life and
  3DMark 99 were the validation workloads for all of it. Final Reality is
  the first workload since then whose records carry real per-record state
  changes with a run pending, which is the path at `d3d_core.c` around the
  `state_before` copy and the flush under the saved context. That is a
  hypothesis from reading, not a measurement.

The host gates (`build-host.ps1`) pass on this tree per the other session's
record; they do not exercise a live context or the Gen3 sink.

## What would settle it

1. **Control.** Swap `HALOLD.DLL` (working tree at `b182f94`, 160,768 bytes)
   into `C:\WINDOWS\SYSTEM\V9XHAL.DLL` with FR closed, run Robots once. If
   it survives, the fault is in the uncommitted diff; if not, bisect the
   six commits above. The netbook is shared with the record-merging
   session, so this is theirs to schedule.
2. **A per-call record count and a "last record shape" pair** in the
   diagnostics block, so the next snapshot says whether the fatal call
   counted anything and what its first record looked like.
3. **The crash dialog's Details.** The operator can read module and offset
   from the "illegal operation" box before dismissing it; `V9XHAL.DLL` plus
   an offset against the map file names the function.

Until 1 is done, the record-merging HAL should not ship: Final Reality is in
the 0.7.0 hardware Direct3D acceptance set (STATUS.md) and it does not
survive its first test on the Gen3 path.

## Cause: the list builder's 6 KB staging on an application stack with 16 KB committed (2026-09-30, fixed)

Measured on the netbook over wifi (10.0.1.254), boots 72-77, each HAL
installed by a WININIT rename and confirmed by hash, Robots only, one pass,
driven through the agent's `input` verb. The fault is not in the
record-replay diff and not in Gen3: it is stack depth, introduced by
`08b4239` (in-call batch merging), which gave `v9x_d3d_draw_list` a
6,144-byte `staging` array on the stack.

- **Boot 72, HEAD (`a9a0fef`) unmodified:** same death as boot 71, five
  DrawPrimitives calls, one record. No fault dialog appeared, and
  `C:\V9XDIAG\V9XTRACE.INI` was not written, so the HAL's unhandled
  exception filter never ran. `DpPrimTypeSeen=0x40`: every record is a fan.
- **Boots 73-74, breadcrumbs pushed into the trace ring** (temporary, not
  committed). The fatal call is record 0 = a 4-vertex fan (appended, 2
  triangles pending), record 1 = the terminator carrying one pair,
  `TEXTUREHANDLE = 0`. That state change flushes the pending run
  (`0x71000002` pushed), and the process dies before `v9x_d3d_draw_list`
  pushes its first breadcrumb. It is the first geometry Final Reality ever
  sends through DrawPrimitives; the four earlier calls carry terminators
  only.
- **Disassembly:** `v9x_d3d_draw_list` is `sub esp,0x1848` (6,216 bytes)
  and first touches the bottom of that frame at the breadcrumb call that
  never landed; `v9x_d3d_draw_primitives_body` is `sub esp,0x1ad4` (6,868).
- **Boot 75, `fs:[4]`/`fs:[8]` and a local's address in
  `v9x_d3d_records_batch`:** stack base `0x00750000`, committed limit
  `0x0074C000`, ESP about `0x0074D734`. `draw_list`'s frame therefore bottoms
  at about `0x0074BEC0`, in the first page below the committed limit.
- **Boot 76, `VirtualQuery` of that page:** allocation base `0x00640000`
  (a normal ~1 MB reservation), `MEM_RESERVE`, `PAGE_NOACCESS`, one page.
  The stack was not exhausted. Its first touch of a new stack page, from
  inside the HAL callback, killed the process instead of growing the stack.

Why growth failed is not established. The absent dialog and absent
`V9XTRACE.INI` fit the Direct3D runtime catching the exception around the
HAL call before any stack growth happens, but nothing here measured that.
It is also why stack probing was not taken as the fix: a probe is only a
first touch of the same page.

**Fix:** `v9x_d3d_draw_list`'s staging is file-scope storage
(`v9x_d3d_list_staging`), shared by its three callers under the Win16 mutex
DirectDraw holds around every HAL callback
(`../decisions/2026-09-26-98se-directdraw-holds-the-win16-mutex-around-every-hal-callback-measured.md`);
the function is never re-entered. Its frame is now 72 bytes.

**Boot 77, fixed HAL:** Robots ran to completion and Final Reality returned
to its options dialog; the frame rendered correctly
(`netbook-boot77-fixed-robots-frame.png`). After the run:
`D3dRenderPrimitiveCalls=87007`, `DpRecords=707040`,
`DpRecordTriangles=749923`, `DpRecordRuns=387573` = `I9xxDrawsSubmitted`,
`I9xxDrawsRefused=0`, `BatchesEngineRefused=0`. So on this workload the
record-merging code itself is sound: 707,040 records went out in 387,573
batches.

Gates: `check-tree.ps1`, `build-host.ps1`, `run-checks.ps1` pass.

Not done:

- **Rage Mobility (Mach64), Gateway SOLO2150 at 10.0.1.22:** the pre-fix
  HAL (the `dp-record-merge-ati` job's, ABI 2026093003) reproduced on boot
  77 with the netbook's signature: five DrawPrimitives calls, one fan
  record (`DpPrimTypeSeen=0x40`), `M64Draws=0`, the fifth call entered
  and never exited, then DirectDraw's cleanup
  (`gateway-boot77-prefix-crash-V9XSNA7.INI`). The fixed HAL (`22a34b7`
  build, hash-verified after the rename) was installed for boot 78 and
  Robots started. About 25 s in, the agent stopped answering; within two
  minutes its port refused connections while the machine still answered
  ping, and it stayed that way for over six minutes. No snapshot or screen
  was obtained, so whether Robots ran, the HAL locked, or this is the
  Gateway's open intermittent lock
  (`2026-09-30-gateway-intermittent-black-screen-probe-lock.md`) is not
  known.

  **Boot 79 (after a manual power cycle), same fixed HAL:** Robots run
  again, with no agent traffic during the run. It completed, Final Reality
  returned to its options dialog and exited cleanly. Counters
  (`gateway-boot79-fixed-post-V9XSNA7.INI`): `D3dRenderPrimitiveCalls=80014`,
  `DpRecords=649829` in `DpRecordRuns=356509` = `M64Draws`,
  `M64Triangles=679252`, `M64Refused=0`, `BatchesEngineRefused=0`, and zero
  FIFO, idle, flip-wait, breadcrumb and ring-space timeouts and resets. The
  fix holds on the Mach64. Boot 78 left no `V9XTRACE.INI`; its agent log
  ends at a screenshot request 25 s into that run. Whether the screenshot
  caused that lock is not established.
- **What remains on the stack:** the DrawPrimitives body's 6 KB run buffer
  (6,868-byte frame) and DrawOneIndexedPrimitive's 6 KB gather (6,292) are
  still stack frames, the same depth as before `08b4239`, which Final
  Reality survived on 2026-09-20. An application whose thread calls in with
  less committed headroom than about 7 KB would hit the same failure.
- Final Reality's other tests, and a score, were not run.
