# Intel Phase 4: the first write, after a no-write capture boot

Status: approved design; implementation in progress, 2026-09-13. The current driver is still no-write-only: it records `TokenMover=READY` on an unarmed boot, but refuses a non-empty `IntelArmOnce` with `TokenMover=EXECUTOR-ABSENT` and does not consume the token. **Do not perform the armed boot until the executor, Enable-side arm check, and armed validator are implemented and tested together.** Written against `docs/plans/hardware-d3d-on-intel-gma950.md` Phase 4 and its arm-and-disarm section, the risk decision `docs/decisions/2026-09-13-intel-phase4-gate-opened-by-risk-decision.md`, and the measured Phases 1 to 3. The operator accepts a no-write capture boot followed by one armed test boot. The token move belongs in the driver's real load entry, `DriverInit` (called `LibMain` in the proposal), not in a DOS helper.

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

Boot 1 runs the existing no-write capture and supplies this machine's packet CRC. One separately armed boot runs the entire write matrix automatically at the first Enable, records after every step, and tears down before the desktop is usable. The operator boots, waits for the desktop, shuts down, and pulls the stick. Nothing in boot 1 can arm the experiment.

## The sequence, in order, with what each step proves

Steps 0 to 3 write no register. Steps 4 onward are the first writes to Intel silicon.

**0. Preconditions, or refuse.** Phase 1 fingerprint `PASS`, Phase 2 inventory `PASS` with hash `4D8707C5`, Phase 3 boot record with ring disabled and idle, `FlushPageRead=STABLE`, exact identity `8086:27AE` rev 3, not Safe Mode, AC power cannot be sensed so the runbook says so. Any failure: `Result=PRECONDITION-REFUSED`, no GPU write, and the boot continues on the normal Phase 1 to 3 path. If `DriverInit` already moved a token, record the clean refusal, set and verify `IntelEnableThisBoot=0`, then clear `IntelInFlight` only after the refusal record is flushed; never leave a misleading incomplete-reset marker for a known refusal.

**1. Arm contract.** `DriverInit` has already processed `[Velocity9x]` in `SYSTEM.INI` using the transaction below. Enable requires its volatile, per-load armed latch as well as `IntelEnableThisBoot=1`, `IntelInFlight` equal to the token and `IntelArmCrc`. Evaluate `v9x_i9xx_arm_evaluate()` with `errata_gate` true (the risk decision) and `configured_crc` from `IntelArmCrc`. The new boot-1 capture publishes `ArmExecutionCrc`, computed as a streaming CRC over the **whole canonical execution sequence**: first two-dword probe, `16382` wrap `MI_NOOP` dwords, second two-dword probe, and eight-dword BLT stream. Its existing `ArmPacketCrc` remains the ten-dword probe/BLT plan checksum for backwards-readable diagnostics; it is not sufficient to arm the wrap test. The operator copies `ArmExecutionCrc` into `IntelArmCrc`. A stale on-disk `IntelEnableThisBoot=1` alone can never arm a new driver load. Rejection is recorded with its reason code; after a flushed refusal record, disarm and clear the in-flight marker if the profile writes can be verified. Otherwise leave it in place and fail closed.

**2. Intent to disk, disarm on disk.** Before even staging memory, `INTELRNG.TXT` gets `Intent=<token>`, `IntentCrc`, `IntentStep=stage`, flushed. `IntelEnableThisBoot=0` is written to `SYSTEM.INI`, flushed and re-read; if the re-read does not say 0, stop. The session's volatile latch remains armed for this one run. From here a power cycle loads an unarmed driver, and the absence of a completion record is the evidence of where it died.

**3. Stage into ring memory, prove the GTT.** The mini-VDD maps the 128 KiB reserve at physical `7FF90000` and writes the guard pattern to scratch and the ten decoded dwords into the ring at offset 0. The 16-bit side reads them back through the framebuffer selector over GMADR at aperture `790000` and `7A1000`. Physical write, aperture read: equality proves the GTT maps our range the way the GPU will fetch it, with no register touched. Mismatch: `Result=GTT-MIRROR-FAILED`, stop. This is the first write to memory the VBIOS owns; it is offscreen heap memory nothing else uses, and it programs no hardware state. Record `IntentStep=execute` and flush before the first register store.

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

**6. The probe.** TAIL `2030` gets `8`: two dwords, `MI_NOOP` then `MI_FLUSH`. Poll HEAD with an iteration bound of one million reads and a wall-clock bound of 200 ms from `Get_System_Time`. Record the tick count and iteration count at which HEAD reached 8. Timeout: record HEAD, TAIL, the error registers and `Result=PROBE-TIMEOUT`, then latch poison for the session. Do not submit again or rewrite HEAD/START/CTL on an engine not proved idle; leave `IntelInFlight` intact and require a power cycle after preserving the log.
The poll compares `(HEAD & 001FFFFCh)` with the expected byte offset, while
recording the unmasked HEAD: Linux's [ring register definition](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/gt/intel_engine_regs.h)
separates `HEAD_ADDR` from `HEAD_WRAP_COUNT`. This matters at the zero wrap.

**7. Wrap.** Only after step 6 drained with HEAD=TAIL=8. The mini-VDD fills every dword from byte offset 8 through the last dword of the 64 KiB ring with `MI_NOOP`, using the physical mapping. This is exactly `ring_bytes - 8` bytes, the maximum legal occupancy with an eight-byte empty slot. It writes TAIL to **0**, not to the last qword; HEAD must consume the final qword and wrap to 0, bounded as in step 6. Only then write a second two-dword probe at offset 0, set TAIL to 8, and require HEAD=8. Stop if the ring-space calculation does not admit each submission. This proves the ring length in RING_CTL is what the hardware believes, which is the one number a wrong RING_CTL would silently break.
Linux's [ring code](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/gt/intel_ring.c)
notes that a wrap at offset zero sometimes hangs; on this machine this step
is an explicit risk test under the accepted hang rule, not a benign capacity
check.

**8. The blit.** Eight dwords, `XY_COLOR_BLT` of an 8 by 8, 32 bpp block at scratch plus `100h`, colour `55AA33CC`, followed by `MI_FLUSH` and `MI_NOOP`. TAIL advances by 32. After HEAD catches up, the 16-bit side reads the scratch page through GMADR: the 64 target dwords must equal the colour, and every guard dword outside the target must still equal the guard pattern. This is the only step that proves the GPU wrote memory and that a GTT offset in a command means what we think it means. Mismatch is recorded with the first differing offset; `Result=BLIT-MISMATCH`.

**9. Teardown to the measured boot state.** Reach this only after the last submitted TAIL has been observed at HEAD. Write RING_CTL `0`, RING_HEAD `0`, RING_TAIL `0`, RING_START `0`, each read back. This is what Phase 3 measured at every quiet point, so the post snapshot has a known target. A timeout or ambiguous HEAD never enters this step; the in-flight marker stays and no further GPU write is made.

**10. Post snapshot and completion.** Phase 3 event capture, kind 8 `ring-post`, plus the error registers. Requirements: ring registers all zero, PGTBL_CTL `7FFC0001`, GTT hash `4D8707C5`, fences zero, error registers unchanged from step 4. `Result=PASS` only if every step recorded success. The display fingerprint is re-run and must match Phase 1 to the dword: the display was never ours to touch, and this proves it. After a clean, read-back-verified teardown, write and flush `IntelLastResult=pass:<token>` (or `fail:<reason>:<token>`), then clear and flush `IntelInFlight`. A teardown that cannot be verified leaves `IntelInFlight` intact; it must not claim clean completion.

Steps 7 through 10 are skipped, and recorded as skipped, if step 6 timed out. The same rule applies to either wrap wait or the blit wait. Nothing retries within a boot.

## Read-only registers added to the pre and post snapshots

From the Linux register header, documentation-derived and unconfirmed on this machine, treated as Phase 1 treated its allowlist: IPEIR `2088`, IPEHR `208C`, INSTDONE `2090`, ACTHD `20C8`, EIR `20B0`, ESR `20B4`, EMR `20B8`. They satisfy the plan's "no error register set" criterion and, on a timeout, say where the parser stopped. No write to any of them.

## Gates, and where each lives

- **16-bit policy**, host-testable: arm evaluation, precondition checks, the sequence state machine, CRC, decode. Arm evaluation, ordering gate, CRC and decode are pure functions; the hardware-dependent precondition checks still need to be connected to the sequencer.
- **Sequence gate detail.** `v9x_i9xx_phase4_sequence_commit()` accepts only the next ordered, fully recorded step from preflight through post-snapshot. An out-of-order or repeated step poisons the session; a timeout explicitly poisons it. A clean image mismatch can still complete the `VERIFY` step with a failed *result* and proceed to teardown, because the BLT drain was observed. The gate is host-tested now but must be wired to the executor before `V9X_I9XX_FIRST_WRITE_EXECUTOR` is enabled.
- **Mini-VDD execution**, API v6, three functions. `RING_STAGE`: up to 32 dwords into VxD storage plus the guard pattern, index-bounded. `RING_EXECUTE`: takes the expected `ArmExecutionCrc` and a step selector; recomputes it over the staged probe/BLT dwords **and the deterministic wrap fill and repeated probe**, without allocating a 64 KiB near buffer, and refuses on mismatch. A stale or altered stream cannot reach a register even with a valid token. It runs exactly one of steps 5, 6, 7, 8, 9; returns status, read-backs, tick and iteration counts through the register set the existing API already uses. `RING_MEMORY`: reads back a ring or scratch dword through the physical mapping, for the diagnostic mirror.
- **Tree checks.** Every store through the BAR0 mapping lives in one procedure, `V9xMini_I9xx_Ring_Execute`, whose store count and offsets are pinned by check-tree the way the read counts are today. That procedure is reachable only from the `RING_EXECUTE` dispatch arm, which is behind the positive Intel family guard and the CRC compare. A `mov [esi+...]` anywhere else in the Intel mini-VDD fails the tree.
- **Never in the VxD:** string handling, `SYSTEM.INI` access, or any decision about whether to arm. It executes an exact stream whose CRC the 16-bit side was authorised for, or it refuses.

## Approved deviation: token transfer in `DriverInit`

The 2026-09-13 approval replaces the DOS helper with the Intel build's 16-bit
display-driver load entry. This codebase exports `DriverInit`, not `LibMain`;
`DriverInit` calls `v9x_display_boot_log()` before any Enable. The token
transaction runs there. A volatile latch starts false on every load and is the
only authority for the later Enable to attempt the arm contract. In
particular, a stale `IntelEnableThisBoot=1` on disk is not authority.

The transaction is fail-closed and ordered for a power cut between any two
profile writes:

1. Clear and flush `IntelEnableThisBoot=0` first. On write, flush or read-back
   failure, keep the volatile latch false and do not experiment.
2. If `IntelInFlight` is non-empty, record and flush
   `IntelLastResult=incomplete-reset:<token>`, leave `IntelInFlight` intact,
   and stop. This applies even if `IntelArmOnce` is also non-empty. A second
   `DriverInit` in the same boot also fails closed; it cannot arm twice.
3. If `IntelArmOnce` is empty, stop. This is the expected first/no-write boot.
   Otherwise validate its bounded character set and the configured CRC
   format; refuse malformed input without changing `IntelArmOnce`.
4. Write and flush `IntelInFlight=<token>`, then re-read exact equality.
   Only after that succeeds, clear and flush `IntelArmOnce` and verify empty.
   A failure here leaves an in-flight marker that makes the next load unarmed.
5. Write and flush `IntelEnableThisBoot=1`, re-read it, and only then set the
   volatile latch for this load. Enable repeats the token/CRC/identity checks
   using that latch. Any profile I/O uncertainty leaves the latch false.

The no-write build compiles the transfer code but cannot enter steps 4 or 5:
the positive `V9X_I9XX_FIRST_WRITE_EXECUTOR` build guard is absent. A queued
token is reported as `EXECUTOR-ABSENT`, left in `IntelArmOnce`, and cannot
accidentally be consumed by a build with no execution path. Adding the
executor must also add and test the Enable-side volatile-latch check before
enabling that guard.

There is no reliance on an early INI write succeeding: a missing or
unwritable `SYSTEM.INI` yields an ordinary unarmed boot. The same profile API
already appears in the driver, but its early-boot success on this machine must
be proved by the unarmed boot's diagnostic before any token is installed:
`INTELRNG.TXT` records `TokenMover=READY` only after the step-1 write, flush
and read-back succeeded with no in-flight or once token. The host validator
requires that field on boot 1. Safe Mode ignores all arm keys; it does not load
this display driver. The mini-VDD never reads or writes `SYSTEM.INI`.

## Operator procedure for the trip

1. Deploy the package as usual: copy over `WINDOWS\SYSTEM` from F8 DOS, check the build id.
2. Boot once unarmed. This produces the no-write `INTELRNG.TXT` with this machine's `ArmExecutionCrc` and `TokenMover=READY`, and confirms Phases 1 to 3 still pass under the new mini-VDD. Copy `V9XDIAG`. Do not install a token if either the ring-plan validator or the token-mover round trip fails.
3. On the host, validate the capture, take `ArmExecutionCrc`, and write the arm keys into the stick's `WINDOWS\SYSTEM.INI` from here:

```ini
[Velocity9x]
IntelAccelDefault=0
IntelArmOnce=p4-20260913-a
IntelArmCrc=<ArmExecutionCrc from step 2>
IntelInFlight=
IntelEnableThisBoot=0
IntelLastResult=
```

4. Boot on AC. Wait for the desktop, do nothing else, shut down. Photograph the desktop once; the display must be untouched.
5. Pull the stick, copy `V9XDIAG`, run the ring validator. It recomputes the CRC, checks every step's read-backs and the guard pattern, and diffs pre against post.
6. If the machine hangs: photograph, power-cycle, boot. `DriverInit` records `incomplete-reset:<token>` and that boot is unarmed. Copy `V9XDIAG`; `IntentStep` says how far it got. Per the risk decision, re-arm the identical experiment once by explicitly clearing `IntelInFlight`, restoring the **same token** to `IntelArmOnce` and keeping the same CRC. A second reproducing hang is ours; a third hang of any kind ends the phase.

The normal path is two boots, one capture each. The first supplies the CRC that
pins the second boot's stream; a hang requires the additional recovery boot.

## Implementation slices, each gated green before the next

1. Header and contract: API v6 constants, event kinds 7 and 8, error-register offsets, `SYSTEM.INI` key names; check-tree contract entries.
2. Pure state machine and `SYSTEM.INI` policy in C with host tests, including the incomplete-reset path and the "step 8 without step 6" refusal.
3. Mini-VDD: physical mapping of the reserve, `RING_STAGE`, `RING_MEMORY`, `RING_EXECUTE` with the pinned store table; check-tree pins.
   API v6 and exact ten-dword physical-RAM staging are implemented. The MMIO
   executor is source-guarded by `V9X_I9XX_FIRST_WRITE_EXECUTOR`, a symbol no
   package build defines. It was separately syntax-checked with MASM but is
   not link-reachable in a package until the Enable-side arm and validator
   slices pass. No current package can submit a ring command.
4. 16-bit sequencer and `INTELRNG.TXT` schema (intent, per-step results, read-backs, tick counts, mirror and guard results, pre and post).
5. Validator extended for the armed file; self-test fixtures for a clean run, a read-back mismatch, a guard breach and a timeout.
6. Runbook 3.2e and the diagnostics contract.

## Rules

No register outside the five ring registers is written, in any step. No GTT write, no fence, no HWS_PGA, no interrupt enable. Nothing here publishes a capability; DirectDraw and Direct3D are unchanged. A hang is handled by the risk decision's rule, not by trying something else.
