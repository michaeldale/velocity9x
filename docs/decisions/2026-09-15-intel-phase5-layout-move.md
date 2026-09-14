# Grow the Intel reserve to 1 MiB, and stop three copies of one CRC drifting

**Date:** 2026-09-15
**Status:** Step 2 of `docs/plans/intel-gma950-phase5.md` Part 2, complete on the
host. **Nothing has been booted** - not the netbook, not a guest.
**Depends on:** `2026-09-14-intel-gen3-3d-packet-audit.md` (step 1), which fixed
the render target's format and therefore its size.

## What moved

`V9X_I9XX_GTT_RESERVE_BYTES` 0x20000 -> 0x100000. The reserve is carved from
the **top** of the VBE-reported region, so growing it moves its base downward
into pages the Phase 2 inventory already proved present and linear. No PTE is
written and nothing extends past the region's end.

At the measured netbook inputs (`vbe 0x7b0000`, `bsm 0x7f800000`):

| Region | Aperture offset | Bytes | Physical |
|---|---|---|---|
| DirectDraw heap | `0` | `0x6b0000` | - |
| ring | `0x6b0000` | `0x10000` | `0x7feb0000` |
| HWS (carved, never written) | `0x6c0000` | `0x1000` | `0x7fec0000` |
| scratch / target lower guard | `0x6c1000` | `0x1000` | `0x7fec1000` |
| **render target** | `0x6c2000` | `0x96000` | `0x7fec2000` |
| upper guard | `0x758000` | `0x1000` | `0x7ff58000` |

Every row matches the plan's table exactly. The guard ends at `0x759000`,
inside the reserve's `0x7b0000` end.

`struct v9x_i9xx_sandbox_layout` gained six appended `v9x_u32` members -
`target_offset`, `target_physical`, `target_bytes`, `target_pitch`,
`guard_upper_offset`, `guard_upper_physical`. They must all be `v9x_u32`
because `v9x_i9xx_zero_layout` walks the struct as a `v9x_u32` array; appending
is safe because no positional initialiser of this struct exists anywhere.

The scratch page is deliberately still at `reserve + 0x11000` whatever the
reserve size, which is what leaves `loader.asm`'s guard probes at `+0x11000`
and `+0x11ffc` unchanged. Only the base moved.

`v9x_i9xx_sandbox_calculate` now also *checks* that the upper guard fits inside
the reserve rather than asserting it in a comment. The target size and the
reserve size are separate constants, and a future mode change that grew one
without the other would otherwise put part of the target in the published heap.

## The execution CRC, and a near-miss worth recording

Phase 4's BLT destination is `scratch + 0x100`, which is inside the reserve, so
the layout move changed the stream and with it the execution CRC:
**`3EAA137B` -> `A0DA64A1`**. The currently armed stick is invalidated and must
be re-armed.

The plan warned that `check-intel-ring-plan.ps1 -ComputeArm` is a fourth
reimplementation and "the copy that will drift", so the new value was taken
from the compiled C builder and only then compared with PowerShell's. They
agree.

**They appeared not to at first, and the reason is a real trap in the API.**
`v9x_i9xx_build_color_blt` writes **six** dwords; `v9x_i9xx_phase4_execution_crc`
reads **eight**. The caller (`intel_ring16.c:126-127`) appends `MI_FLUSH` and
`MI_NOOP` itself. A first version of the new host test declared `blt[6]` and so
hashed two dwords of stack, producing a mismatch that looked exactly like a
C-versus-PowerShell drift and was not. Recorded because the next person to
write a test against this CRC will make the same mistake: **the array is eight
long and the builder fills six of it.**

## The three-way drift path, closed

The execution CRC existed in three places and nothing compared them:

1. `v9x_i9xx_phase4_execution_crc`, the C builder the driver actually runs;
2. the `03eaa137bh` literal in `loader.asm` the mini-VDD refuses to execute
   without;
3. `check-intel-ring-plan.ps1 -ComputeArm`, a PowerShell reimplementation.

`tests/host/test_i9xx_arm.c` did assert the CRC, but against a **hand-written**
stream, so the layout move did not break it - it simply left it asserting a
stream the driver will never build again. That is the worst case: a test that
still passes while describing something untrue.

A new host test, `test_phase4_execution_crc` in `test_i9xx_ring.c`, builds the
stream through `v9x_i9xx_sandbox_calculate` and `v9x_i9xx_build_color_blt` from
the layout, and asserts both the destination and the CRC. It makes the C
builder the source of truth and turns a three-way drift into a test failure.
`test_i9xx_arm.c` was updated to the stream the driver now builds.

This is a partial fix. The `loader.asm` literal is still hand-maintained and
nothing mechanically compares it to the C value; that is step 4's generator,
and it remains the last drift path in the chain.

## Re-deriving the reserve's backing on every boot

The plan asked for this rather than leaning on the 2026-09-12 Phase 2 capture,
and it is the assertion the enlarged reserve actually needs: the base moved
896 KiB down, and the claim that the reserve lies inside the measured backed
prefix should be checked, not argued once.

`intel_gtt16.c` now retains `backed_prefix_entries`, `reserve_first_entry` and
`reserve_entry_count` - all three were already computed and published to
`INTELGTT.TXT`, but nothing kept them where the preflight could read them. The
Phase 4 preflight gains `V9X_P4_PRE_RESERVE_BACKING` (19), which refuses unless
`reserve_first + reserve_count <= backed_prefix`, with an overflow guard on the
addition. All three operands, plus the new target and guard offsets, are
published as `Ref*` keys so a refusal names numbers rather than a bare code -
the Phase 4 record's own prescription for why that phase cost eight boots.

## The heap shrink

The published DirectDraw heap fell from `0x790000` to `0x6b0000`, 896 KiB
smaller. A new host test asserts all four published modes still clear it:

| Mode | Visible bytes | Headroom |
|---|---|---|
| 640x480x8 | `0x04b000` | 22.8x |
| 1024x576x8 | `0x090000` | 11.9x |
| 640x480x16 | `0x096000` | 11.4x |
| **1024x576x16** | `0x120000` | **5.9x** |

The binding case is named explicitly so that adding a larger mode fails there
loudly. Two honest qualifications: the test also asserts the heap ends exactly
where the reserve begins, and **nothing currently exercises DirectDraw
allocation on this family at all** - its `EngineType` is `NONE` and no surface
is ever allocated from this heap. The number is published and unused.

## Six stale fixtures, all found by the gates

Worth listing, because it is the clearest evidence this project's gates earn
their keep. Each was found by a check failing, not by reading:

- `check-intel-gtt-capture.ps1`'s own `$reserveBytes`, and separately its
  self-test **mutation target** - the mutation named a line that no longer
  existed, so it became a no-op and the self-test correctly reported that it
  had accepted a mutation.
- `check-intel-ring-plan.ps1`'s `reserve + 0x20000` bound and its
  "reviewed 128-KiB sandbox" message, then six values in its capture fixture
  including two recomputed CRCs (`BltCrc` `270BDC4C`, `ArmPacketCrc`
  `BA0895B6`).
- `arm-intel-phase4.ps1`'s three copies of the old CRC.
- `test_i9xx_gtt.c`'s synthetic 64-page region, which was larger than the old
  32-page reserve and smaller than the new 256-page one, so the inventory
  refused it. Scaled by eight to keep the same shape.

The plan's review grep - `grep -rn "0079\|7ff9\|007a1" src scripts tests` - now
returns nothing outside `docs/`.

## Gates run

- `build-host.ps1` and `build-host-msvc.ps1` - pass.
- All five families build and audit. intel-gma `_TEXT` 40,905 unchanged,
  `I9XXCODE` 21,329 -> 21,875 for the preflight and its published operands;
  DGROUP 11,032 -> 11,044. Every other family byte-unaffected.
- `run-checks.ps1` - pass, including all four Intel capture validators and the
  Phase 4 arm script's self-test.

## Not tested, and one thing invalidated

Nothing here has been booted. The netbook has not seen this layout, and 86Box
has no GMA 950 to see it in.

**The armed stick is invalidated.** Its `IntelArmCrc` is `3EAA137B`, and the
driver will now compute `A0DA64A1` and refuse at `V9X_P4_PRE_CRC_MATCH`. It
must be re-armed with `arm-intel-phase4.ps1` before any armed boot. That is the
correct behaviour - a stale arm refusing is the whole point of the CRC - but it
is a thing that must be done, not a thing that will happen.

Also still outstanding from Part 1: the netbook regression boot, which has
never run, and which this change adds to rather than replaces.
