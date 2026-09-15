# Generate the Intel arm tables, and make Phase 5 a distinct arm phase

**Date:** 2026-09-15
**Status:** Step 4 of `docs/plans/intel-gma950-phase5.md` Part 2, complete on
the host. **Nothing has been booted.**
**Depends on:** steps 1-3. Supersedes the hand-maintained constants that
step 2's layout move found stale three ways at once.

## The drift path, closed

Phase 4's execution CRC lived in three places and nothing compared them: the C
builder the driver runs, a literal in `loader.asm` the mini-VDD refuses to
execute without, and a PowerShell reimplementation. The layout move found the
second and third stale simultaneously. Phase 5's stream is six times larger, so
maintaining it by hand was not a plan.

The compiled builders are now the single source of truth. The host test binary
gained `--emit-intel-3d-stream`, which prints both streams and their CRCs from
the same code the driver builds them with, and
`scripts/gen-intel-3d-stream.ps1` renders two checked-in artefacts from that
output:

| File | For |
|---|---|
| `src/minivdd32/i9xx3d.inc` | MASM tables, CRCs and offsets, included by `loader.asm` |
| `scripts/data/intel-3d-stream.psd1` | the same numbers for the validators |

`loader.asm` no longer defines the Phase 4 stream at all; `V9xI9xxRingExpected`
is now an alias for the generated `V9xI9xxPhase4Table`, so every existing
reference keeps working and the table has exactly one definition. The execution
CRC and the ring START value come from the generated EQUs.

Both artefacts are checked in, so a netbook trip is reproducible from a clean
checkout and a human can read the gate without running anything.

### Where the include sits, and why it matters

`i9xx3d.inc` is included **inside the mini-VDD's data segment**, not with the
contract includes at the top of `loader.asm`. It emits data, so it must land in
a data segment; included at the top it assembles into whatever segment happens
to be current. This is recorded because the obvious placement is the wrong one.

## Two halves of one check, split by what needs a compiler

The plan asked for this split and it turned out to matter more than expected.

**`check-tree.ps1` does the compiler-free half.** The `.inc` exists, carries its
generated banner, `loader.asm` holds no literal table and does include the
generated file - and, the part that earns its keep, **PowerShell recomputes
CRC-32 over the parsed table and asserts it equals the EQU literal in the same
file**. A hand edit of either the table or the CRC changes one without the
other, which is exactly what that comparison catches. The combined CRC is
likewise recomputed over the two phase CRCs.

**`run-checks.ps1` does the half that needs one**, immediately after
`build-host.ps1` so it uses the binary just produced:
`gen-intel-3d-stream.ps1 -Verify` regenerates and diffs byte for byte.

All three gates were verified to fire by deliberately editing a table dword, a
CRC literal, and the combined CRC.

### The check caught a real error in its own design

The first version of the `check-tree` rule recomputed CRC-32 over the Phase 4
table and compared it with `V9X_I9XX_P4_CRC`. It failed - correctly. Phase 4's
**execution** CRC covers the probe, a full-ring wrap of 16,382 NOOPs, the probe
again and then the BLT; it is not derivable from the ten staged dwords at all.

So the emitter now produces both: `V9X_I9XX_P4_PACKET_CRC` over the table,
which is what the compiler-free check compares, and `V9X_I9XX_P4_CRC` for
execution, which only the compiled builder can know and which
`gen-intel-3d-stream.ps1 -Verify` is responsible for. Phase 5 needs no such
split, because its execution CRC **is** the CRC over its whole stream.

The generated values, for the record: Phase 4 packet `BA0895B6`, Phase 4
execution `A0DA64A1`, Phase 5 `78780722`, combined `09F28B92`.

## Phase 5 is a distinct arm phase

`V9X_I9XX_PHASE5` exists, and `v9x_i9xx_arm_evaluate` no longer hardcodes
Phase 4 on both sides of its phase check. The arm request gained
`expected_phase` - what the caller is arming, as distinct from what the token
claims - and the two must match.

This is not bookkeeping. The 2026-09-13 risk decision opens the errata gate for
**Phase 4 specifically**, on an argument about minimal processor-to-graphics
interaction that does not transfer to a triangle. Without a distinct phase, a
token issued under that decision would silently authorise a 3D draw.

The sequence machine now takes a step range, so Phase 4 (steps 1-12) and
Phase 5 (steps 20-30) have disjoint numbering and a log line names its phase
from the step alone. `v9x_i9xx_phase4_sequence_begin` is a thin wrapper, so
`intel_exec16.c` and its existing tests are untouched.

## The two-phase arm transaction

Phase 5 replays Phase 4 first, because Phase 4 is the only thing that can
distinguish "the layout move broke the ring" from "the 3D packets hung the
parser". But a replay inside a Phase 5 run is not a Phase 4 run: the standalone
path records its result and clears `IntelInFlight` on success, which would
retire the token before the draw it was issued for had happened.

`src/chipsets/intel/i9xx_chain.c` makes it one transaction over two executions.
The three properties the plan requires are asserted directly by host tests:

- **A Phase 4 token cannot reach Phase 5.** Both phase fields are checked, and
  separately the armed CRC must be the **combined** one covering both streams
  in execution order - so a Phase 4 token is refused even with its phase field
  forged.
- **A Phase 5 token cannot skip a failed replay.** A draw may only be reported
  from the state a passed replay produces, and the replay must satisfy
  Phase 4's own generated CRC. Chaining widens what may be armed; it does not
  relax what each half has to prove.
- **A power cut between the phases leaves `IntelInFlight` set.** The token is
  retired in exactly one place - a completed draw - and every failure path
  marks the chain failed *without* clearing in-flight. The interesting case is
  the middle: Phase 4 has completed successfully and the token is still in
  flight, because the draw it authorised has not happened.

The standalone Phase 4 arm path keeps working with its own CRC.

## Gates run

- `build-host.ps1` and `build-host-msvc.ps1` - pass.
- `build-minivdd-skeleton.ps1` - the mini-VDD assembles against the generated
  tables.
- `check-tree.ps1` - pass, with the new generated-table rule.
- `run-checks.ps1` - pass, now including the generator verify step.
- All three new gates verified to fire by deliberate edits.

## Not tested, and what is still hand-maintained

Nothing here has been booted. The generated tables have never been armed, and
the chain has never run against hardware - `i9xx_chain.c` is pure state
machine with no caller yet, exactly like step 3's builders.

**One thing is still hand-maintained and should be said plainly.** The chain's
three entry points exist and are tested, but nothing in `intel_exec16.c` calls
them: the standalone Phase 4 path is still what a Phase 4 arm runs, and there
is no Phase 5 executor to call the chain. Wiring the dispatcher into the driver
is step 6's work, and until then the transaction is a contract nothing enforces
at run time.

Also outstanding from earlier steps: the netbook regression boot, re-arming the
stick after step 2 invalidated it, and the errata-gate extension decision that
Phase 5 needs before B2 - which the `expected_phase` split above makes
enforceable but does not make.
