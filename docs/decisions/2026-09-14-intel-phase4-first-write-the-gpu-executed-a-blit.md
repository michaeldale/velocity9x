# Intel Phase 4 measured: the ring accepts commands and the GPU executed a blit

Date: 2026-09-14
Status: accepted. **Phase 4 of `docs/plans/hardware-d3d-on-intel-gma950.md` is
complete.** This is the first write to Intel silicon in this project. Phase 5,
the first triangle, is unblocked.
Machine: MICHAEL-NETBOOK, HP Mini 110-1000, 945GSE / GMA 950, `8086:27AE`
rev 03, Windows 98 SE from the live USB stick, desktop at 1024x576x8, AC power.
Package: `build\win98se-intel-gma`, build `1f386d0`. Token
`p4-20260914-1f386d0`, execution CRC `3EAA137B`.
Artefact: `2026-09-14-intel-phase4-first-write-INTELRNG.txt`; raw capture under
`build\driver-results\netbook-intel-phase4-20260914-pass\`.

## What ran

One armed boot, unattended, at the first Enable. `Result=PASS` and
`IntelLastResult=pass:p4-20260914-1f386d0` with `IntelInFlight` cleared, which
is the completion record the design asked for.

| Step | What it did | Result |
|---|---|---|
| stage | 10 dwords and a 4 KiB guard written and read back through GMADR | `StageMirror=PASS` |
| pre | MMIO fingerprint and seven error registers | `PreSnapshot=PASS` |
| S05 | RING_CTL, HEAD, TAIL, START programmed, each read back | `Ctl=0000F001`, `Start=00790000` |
| S06 | `MI_NOOP` + `MI_FLUSH`, TAIL=8 | head reached `00000008`, 0 polls, 0 ms |
| S07 | wrap: 16382 `MI_NOOP` to the end of the ring | head `00200000`, 112 polls, 1 ms |
| S08 | second two-dword probe | head `00200008`, 0 polls |
| S09 | one `XY_COLOR_BLT`, 8 dwords | head `00200028`, tail `00000028` |
| S10 | scratch verified | `ScratchGuard=PASS` |
| S11 | teardown to all zeros | `Ctl=00000000`, `Start=00000000` |
| S12 | post snapshot | `PostSnapshot=PASS` |

## What it proves

- **The ring accepts commands.** Head advanced to match tail at every
  submission. The audit's claim that Gen3 has no MMIO-immediate path and that
  everything goes through a ring is now confirmed working on this machine.
- **Wrap works, and the ring length in RING_CTL is what the hardware
  believes.** After 16382 NOOPs the head wrapped: address bits zero with the
  wrap-count field set, which is `00200000` unmasked. The design anticipated
  this and polls `HEAD & 001FFFFC`, which is why the step passed rather than
  hanging on a comparison against a raw zero.
- **The GPU wrote memory.** `XY_COLOR_BLT` filled an 8 by 8 block of
  `55AA33CC` at scratch offset `100`, and every guard dword around it still
  read `A5A5A5A5`. Head reaching tail proves consumption; this proves
  execution and that a GTT offset in a command means what we thought.
- **Nothing else moved.** The pre and post MMIO snapshots are identical to
  each other and to the Phase 1 baseline: PGTBL `7FFC0001`, pipe B live with
  1344x672 totals, plane enabled, stride `400`. The display was never ours and
  was never touched.
- **No error was latched.** EIR and ESR read zero before and after. EMR reads
  `FFFFFFFF` and INSTDONE `7FFFFFC0` in both snapshots, unchanged.

## Timing, as measured

The wrap step drained 16382 NOOPs in 1 ms across 112 polls. The two-dword
probes and the blit each completed before the first read-back, at 0 polls and
0 ms. That is the only engine throughput figure this project has for this
chip, and it is a floor rather than a measurement of the engine.

## What it took, and what that says

Eight armed boots. Seven of them failed, and only one failure was the
hardware telling us something:

| Cause | Boots |
|---|---|
| A `GetSystemMetrics` call importing USER, so the driver never loaded | 3 |
| A missing `push si`, so a caller's struct read was corrupted | 3 |
| CPU writes to stolen memory not being routed | 1 |

The last is the real finding
(`2026-09-14-cpu-writes-to-stolen-memory-need-the-aperture.md`). The first two
were ours, and both were found only after instrumentation that should have
existed from the start: every refusal in this path now carries a numbered
reason and the operands it compared, and both defects became obvious the boot
after that arrived. On a machine with no serial port, a boolean returned from
a twenty-condition helper is not a diagnosis, it is a wasted trip.

## Kill criteria, revisited

None were met. The ring was not firmware-owned, no mapping changed under us,
and no hang occurred, so the hang-interpretation rule from the risk decision
was never exercised. Erratum 12 remains undisclosed and unimplemented; this
workload did not provoke it, which says nothing about a heavier one.

## What Phase 5 inherits

- A ring at aperture `790000`, 64 KiB, programmed and torn down cleanly.
- A verified command path: declare to the mini-VDD, write through GMADR,
  submit by tail write, poll `HEAD & 001FFFFC`.
- Scratch at `7A1000` with a guard convention that catches an over-wide write.
- The knowledge that HWS_PGA never had to be written, and that the VBIOS's
  `1FFFF000` is left alone.
- An unanswered question from the previous record: what the BSM physical range
  actually reads. Nothing in this phase needs it, and no boot should be spent
  on it alone.
