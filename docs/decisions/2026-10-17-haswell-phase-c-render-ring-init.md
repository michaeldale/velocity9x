# Haswell Phase C: the render ring runs our initialising batch and returns

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE, boot 8, with the
B2 driver running the desktop at 640x480x8. `V9XHSWMM.EXE /rcs` with
`HSWMM.VXD` DIOC 3, as designed in `docs\plans\intel-haswell-direct3d.md`
Phase C. Evidence: `docs\probe\hsw-4570-rcs-c-2026-10-17\`.

## Runs 1 and 2: the old VxD answered

Both returned `DIOC-FAILED` (`DiocError=1`, ERROR_INVALID_FUNCTION). The
loader took `C:\WINDOWS\SYSTEM\HSWMM.VXD` (92,111 bytes), the B1 build,
which has no DIOC 3, before the copy in the working directory or `C:\`.
Nothing reached the GPU. Run 3 followed after the system copy was
replaced; the B1 copies are kept off the machine, and the new VxD keeps
DIOCs 1 and 2 unchanged.

## Run 3: every stage

Status `000004FB`, stage 9, `Verdict=init-workload-ran`:

| Stage | Result |
|---|---|
| 1 PTEs | 7 entries `DEA20001`..`DEA26001`, valid, linear from BDSM |
| 2 CPU path | pattern read back; region zeroed, image copied |
| 3 forcewake | held by the driver (ack `1`); used, not requested, not released |
| 4 ring | CTL 0 before; START `01820000`, CTL `00000001`, HEAD 0, TAIL 0 |
| 5 ring store | `C0DE0011` after 2 polls, HEAD `10h` |
| 6 batch A + store | `C0DE0021` from the batch, `C0DE0022` after it, 3 polls, HEAD `28h` |
| 7 init batch + store | breadcrumb `C0DE0031` (high dword 0), then `C0DE0032`, 25 polls, HEAD `40h` |
| 8 record | ACTHD `40h`, IPEHR `10400002`, BBADDR `018222EC`, IPEIR 0, EIR 0, INSTDONE `FFFFFFFE` |
| 9 teardown | CTL, START, HEAD, TAIL all 0 |

Read before the ring was enabled, not written: GFX_MODE `00002800`,
MI_MODE `00000200`, INSTPM `00006080`.

IPEHR `10400002` is the header of the last command executed,
`MI_STORE_DATA_IMM` with GGTT. BBADDR `018222EC` is the address of the
init batch's `MI_BATCH_BUFFER_END`: batch B starts at `1822040h` and is
172 dwords, and its end is dword 171. Both say the engine walked the whole
batch and came back to the ring.

Afterwards, read-only: every RCS ring register 0; the BCS ring as the
driver left it (START `01800000`, CTL 1, HEAD = TAIL); forcewake still
held. A V9XDDF fill and copy at 8 bpp passed, and the desktop was
unchanged.

## Findings

- **RCS executes from a ring we place**, with `MI_STORE_DATA_IMM` to a
  GGTT address, the same as BCS in B1.
- **`MI_BATCH_BUFFER_START`** (opcode `31h`, length 0, first level,
  privileged, GGTT) runs a batch in stolen memory and returns. Privileged
  GGTT stores work inside it.
- **The initialising batch is accepted**: `PIPELINE_SELECT`,
  `STATE_BASE_ADDRESS`, GT2 push constant and URB partitioning (VS 64
  entries), every stage disabled or zeroed, a NULL depth buffer and
  surface, valid pointers for every indirect state, and Vol 2b's
  end-of-batch CC pointers with an RT flush. Its `PIPE_CONTROL` post-sync
  qword write landed. EIR stayed 0. "Accepted" means executed without an
  error being reported. Whether this state is right for drawing is Phase
  D's question.
- **The pointer bit 0 read as MBO** (CC, blend, depth/stencil) was
  accepted. The batch does not show what 0 there would do.
- **Forcewake can be shared with the driver**: the probe ran under the
  B2 engine's hold and left it in place.
- RCS and BCS ran side by side without disturbing each other.

## Not measured

- Any primitive. Nothing was dispatched to an EU.
- RC6 after the render engine has run. Forcewake stays held while the B2
  driver is open, so the GT has not entered RC6 since. Vol 2c's reason
  for the dummy workload is a power context saved and restored on RC6
  entry; that path is untested.
- INSTDONE bit 0 reads 0 while every other bit reads 1. Its meaning on
  Haswell was not looked up.

## Next

Phase D, the first triangle: the PS kernel, vertex setup, a render
target, one `3DPRIMITIVE`. It needs its own design. Ring placement and
the init batch carry over from this phase.
