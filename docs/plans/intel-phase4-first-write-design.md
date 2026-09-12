# Intel Phase 4: the first write, designed to make one boot answer everything

Status: design for approval, 2026-09-13. Written against `docs/plans/hardware-d3d-on-intel-gma950.md` Phase 4 and its arm-and-disarm section, the risk decision `docs/decisions/2026-09-13-intel-phase4-gate-opened-by-risk-decision.md`, and the measured Phases 1 to 3. Nothing below is coded yet.

## What the machine has told us, and what the design leans on

| Measured | Used for |
|---|---|
| VBE usable `7B0000`; reserve at aperture `790000`, physical `7FF90000`, 128 KiB, PTEs linear and valid (Phase 2) | ring `790000` (64 KiB), HWS `7A0000`, scratch `7A1000`, all inside memory the VBIOS mapped and DirectDraw no longer allocates |
| Ring registers all zero, RING_CTL bit 0 clear, at boot, after mode sets, at Disable (Phase 3) | the ring is ours to program and the restore target is "all zero" |
| PGTBL_CTL `7FFC0001`, GTT hash `4D8707C5` unchanged by every event (Phases 2, 3) | no GTT write is needed; a changed hash after our run is a finding |
| Stolen block `7F800000` is UC by MTRR; aperture `D0000000` has no WC range (Phase 1) | CPU stores to ring memory are strongly ordered either way; no explicit flush primitive is assumed |
| BAR0 `FE980000` mapped by the mini-VDD; BAR3 `FE940000` mapped (Phases 1, 2) | the only place a register store can live is the mini-VDD, through the existing BAR0 mapping |
| Full-screen DOS box return hard-locks this machine with zero GPU writes | the test never enters a DOS box; the whole sequence runs at Enable and needs no user input |

## Principle

One armed boot runs the entire matrix automatically at the first Enable, records after every step, and tears down before the desktop is usable. The operator boots, waits for the desktop, shuts down, and pulls the stick. Everything that can be proved before a register is touched is proved first, and the register writes are the last thing that happens.

## The sequence, in order, with what each step proves

Steps 0 to 3 write no register. Steps 4 onward are the first writes to Intel silicon.

**0. Preconditions, or refuse.** Phase 1 fingerprint `PASS`, Phase 2 inventory `PASS` with hash `4D8707C5`, Phase 3 boot record with ring disabled and idle, `FlushPageRead=STABLE`, exact identity `8086:27AE` rev 3, not Safe Mode, AC power cannot be sensed so the runbook says so. Any failure: `Result=PRECONDITION-REFUSED`, nothing armed, the boot is a normal Phase 1 to 3 boot.

**1. Arm contract.** Read `[Velocity9x]` from `SYSTEM.INI`: `IntelEnableThisBoot`, `IntelInFlight`, `IntelArmOnce`, `IntelArmCrc`. Evaluate `v9x_i9xx_arm_evaluate()` with `errata_gate` true (the risk decision) and `configured_crc` from `IntelArmCrc`. The packet CRC is computed from the dwords built and decoded on this boot, so the operator pins the exact stream by copying `ArmPacketCrc` from the machine's own earlier `INTELRNG.TXT`. Rejection is recorded with its reason code and the boot continues unarmed.

**2. Stage into ring memory, prove the GTT.** The mini-VDD maps the 128 KiB reserve at physical `7FF90000` and writes the guard pattern to scratch and the ten decoded dwords into the ring at offset 0. The 16-bit side reads them back through the framebuffer selector over GMADR at aperture `790000` and `7A1000`. Physical write, aperture read: equality proves the GTT maps our range the way the GPU will fetch it, with no register touched. Mismatch: `Result=GTT-MIRROR-FAILED`, stop. This is the first write to memory the VBIOS owns; it is offscreen heap memory nothing else uses, and it programs no hardware state.

**3. Intent to disk, disarm on disk.** `INTELRNG.TXT` gets `Intent=<token>`, `IntentCrc`, `IntentStep=execute`, flushed. `IntelEnableThisBoot=0` is written to `SYSTEM.INI` and re-read; if the re-read does not say 0, stop. From here a power cycle boots unarmed, and the absence of a completion record is the evidence of where it died.

**4. Pre snapshot.** Phase 3 event capture, kind 7 `ring-pre`, plus the error and instruction registers below. Read-only.

**5. Program the ring, reading back each register.** In the mini-VDD, in one call, no interrupts disabled:

| Order | Register | Value | Why this value |
|---|---|---|---|
| 1 | RING_CTL `203C` | `0` | already 0; written so the sequence is the same on a machine where it is not |
| 2 | RING_HEAD `2034` | `0` | |
| 3 | RING_TAIL `2030` | `0` | |
| 4 | RING_START `2038` | `00790000` | GTT offset of the ring, 4 KiB aligned |
| 5 | RING_CTL `203C` | `0000F001` | (65536 minus 4096) or valid, per the audit |

Each write is followed by a read-back; a mismatch stops the sequence before the next write and records `RegisterReadback=<n>`. HWS_PGA is **not** written: the VBIOS's `1FFFF000` stays, the probe needs no status page, and one fewer register is one fewer unknown. After step 5 the ring is enabled with head equal to tail equal to zero, consuming nothing.

**6. The probe.** TAIL `2030` gets `8`: two dwords, `MI_NOOP` then `MI_FLUSH`. Poll HEAD with an iteration bound of one million reads and a wall-clock bound of 200 ms from `Get_System_Time`. Record the tick count and iteration count at which HEAD reached 8. Timeout: record HEAD, TAIL, the error registers, `Result=PROBE-TIMEOUT`, then attempt teardown (step 9) and latch poison for the session. Do not proceed.

**7. Wrap.** Only after step 6 drained. The mini-VDD fills the ring from offset 8 to the end with `MI_NOOP` through the physical mapping, then writes TAIL to the last qword; HEAD must advance to the top and wrap to 0, bounded as in step 6. Then a second two-dword probe at offset 0, TAIL `8` again, HEAD must reach 8. This proves the ring length in RING_CTL is what the hardware believes, which is the one number a wrong RING_CTL would silently break.

**8. The blit.** Eight dwords, `XY_COLOR_BLT` of an 8 by 8, 32 bpp block at scratch plus `100h`, colour `55AA33CC`, followed by `MI_FLUSH` and `MI_NOOP`. TAIL advances by 32. After HEAD catches up, the 16-bit side reads the scratch page through GMADR: the 64 target dwords must equal the colour, and every guard dword outside the target must still equal the guard pattern. This is the only step that proves the GPU wrote memory and that a GTT offset in a command means what we think it means. Mismatch is recorded with the first differing offset; `Result=BLIT-MISMATCH`.

**9. Teardown to the measured boot state.** RING_CTL `0`, RING_HEAD `0`, RING_TAIL `0`, RING_START `0`, each read back. This is what Phase 3 measured at every quiet point, so the post snapshot has a known target.

**10. Post snapshot and completion.** Phase 3 event capture, kind 8 `ring-post`, plus the error registers. Requirements: ring registers all zero, PGTBL_CTL `7FFC0001`, GTT hash `4D8707C5`, fences zero, error registers unchanged from step 4. `Result=PASS` only if every step recorded success. `IntelLastResult=pass:<token>` and `IntelInFlight` cleared in `SYSTEM.INI`. The display fingerprint is re-run and must match Phase 1 to the dword: the display was never ours to touch, and this proves it.

Steps 7 and 8 are skipped, and recorded as skipped, if step 6 timed out. Nothing retries.

## Read-only registers added to the pre and post snapshots

From the Linux register header, documentation-derived and unconfirmed on this machine, treated as Phase 1 treated its allowlist: IPEIR `2088`, IPEHR `208C`, INSTDONE `2090`, ACTHD `20C8`, EIR `20B0`, ESR `20B4`, EMR `20B8`. They satisfy the plan's "no error register set" criterion and, on a timeout, say where the parser stopped. No write to any of them.

## Gates, and where each lives

- **16-bit policy**, host-testable: arm evaluation, precondition checks, the sequence state machine, CRC, decode. Already written as pure functions except the state machine, which is new and small: a step table with a required prior state, so step 8 cannot be reached with step 6 unrecorded.
- **Mini-VDD execution**, API v6, three functions. `RING_STAGE`: up to 32 dwords into VxD storage plus the guard pattern, index-bounded. `RING_EXECUTE`: takes the expected CRC and a step selector; recomputes the CRC over its staged dwords and refuses on mismatch, so a stale or altered stream cannot reach a register even with a valid token; runs exactly one of steps 5, 6, 7, 8, 9; returns status, read-backs, tick and iteration counts through the register set the existing API already uses. `RING_MEMORY`: reads back a ring or scratch dword through the physical mapping, for the diagnostic mirror.
- **Tree checks.** Every store through the BAR0 mapping lives in one procedure, `V9xMini_I9xx_Ring_Execute`, whose store count and offsets are pinned by check-tree the way the read counts are today. That procedure is reachable only from the `RING_EXECUTE` dispatch arm, which is behind the positive Intel family guard and the CRC compare. A `mov [esi+...]` anywhere else in the Intel mini-VDD fails the tree.
- **Never in the VxD:** string handling, `SYSTEM.INI` access, or any decision about whether to arm. It executes an exact stream whose CRC the 16-bit side was authorised for, or it refuses.

## One deviation from the plan, for approval

The plan describes a DOS helper that runs before Windows and moves `IntelArmOnce` to `IntelInFlight`. This design folds that into the 16-bit driver at LibMain, before any Enable: if `IntelInFlight` is non-empty it records `incomplete-reset:<token>`, forces `IntelEnableThisBoot=0` and the boot is unarmed; otherwise it moves the token and sets `IntelEnableThisBoot=1`, exactly the helper's logic. Reason: the live USB stick already runs real-mode DOS before Windows through `IO.SYS`, but a separate helper is one more binary to build, copy and sequence in `AUTOEXEC.BAT` on a blind machine, and every property the plan wanted from it (an incomplete attempt cannot re-arm) holds when the driver does it first thing at load. Safe Mode still ignores every key, because Windows does not load the display driver there. If you prefer the helper as written, the design is unchanged except for who moves the token.

## Operator procedure for the trip

1. Deploy the package as usual: copy over `WINDOWS\SYSTEM` from F8 DOS, check the build id.
2. Boot once unarmed. This produces the no-write `INTELRNG.TXT` with this machine's `ArmPacketCrc` and confirms Phases 1 to 3 still pass under the new mini-VDD. Copy `V9XDIAG`.
3. On the host, validate the capture, take `ArmPacketCrc`, and write the four keys into the stick's `WINDOWS\SYSTEM.INI` from here:

```ini
[Velocity9x]
IntelAccelDefault=0
IntelArmOnce=p4-20260913-a
IntelArmCrc=<ArmPacketCrc from step 2>
IntelInFlight=
IntelEnableThisBoot=0
IntelLastResult=
```

4. Boot on AC. Wait for the desktop, do nothing else, shut down. Photograph the desktop once; the display must be untouched.
5. Pull the stick, copy `V9XDIAG`, run the ring validator. It recomputes the CRC, checks every step's read-backs and the guard pattern, and diffs pre against post.
6. If the machine hangs: photograph, power-cycle, boot. The driver records `incomplete-reset:<token>` and the boot is unarmed. Copy `V9XDIAG`; `IntentStep` says how far it got. Per the risk decision, re-arm the identical token once. A second hang is ours.

Two boots, one file each, and step 2 is the same capture I called a waste yesterday, now doing a job: it produces the CRC that pins the stream.

## Implementation slices, each gated green before the next

1. Header and contract: API v6 constants, event kinds 7 and 8, error-register offsets, `SYSTEM.INI` key names; check-tree contract entries.
2. Pure state machine and `SYSTEM.INI` policy in C with host tests, including the incomplete-reset path and the "step 8 without step 6" refusal.
3. Mini-VDD: physical mapping of the reserve, `RING_STAGE`, `RING_MEMORY`, `RING_EXECUTE` with the pinned store table; check-tree pins.
4. 16-bit sequencer and `INTELRNG.TXT` schema (intent, per-step results, read-backs, tick counts, mirror and guard results, pre and post).
5. Validator extended for the armed file; self-test fixtures for a clean run, a read-back mismatch, a guard breach and a timeout.
6. Runbook 3.2e and the diagnostics contract.

## Rules

No register outside the five ring registers is written, in any step. No GTT write, no fence, no HWS_PGA, no interrupt enable. Nothing here publishes a capability; DirectDraw and Direct3D are unchanged. A hang is handled by the risk decision's rule, not by trying something else.
