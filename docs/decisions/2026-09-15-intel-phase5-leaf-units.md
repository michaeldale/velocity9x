# Phase 5's leaf units, and the golden stream they agree on

**Date:** 2026-09-15
**Status:** Step 3 of `docs/plans/intel-gma950-phase5.md` Part 2, complete on
the host. **Nothing has been booted.**
**Depends on:** `2026-09-14-intel-gen3-3d-packet-audit.md` (step 1) for every
constant, and `2026-09-15-intel-phase5-layout-move.md` (step 2) for the target.

## What exists now

One new header and six new units, all pure C89, all host-tested by both
compilers, none of them reaching the hardware:

| Unit | What it is |
|---|---|
| `include/velocity9x/intel_gen3_3d.h` | Every constant, each citing the audit section that licenses it |
| `i9xx_float.c` | IEEE-754 transport by integer construction, no FPU |
| `i9xx_3d.c` | The 34-dword state block in one reviewed order |
| `i9xx_fragprog.c` | The seven-dword fragment program, no arguments |
| `i9xx_vertex.c` | The 16-dword inline vertex run |
| `i9xx_3d_stream.c` | Stream assembly, parameters, execution CRC |
| `i9xx_3d_decode.c` | The Phase 5 allowlist, returning reason **and** index |

`tests/host/test_i9xx_3d.c` covers them in ten groups, including the golden
stream written out in full.

## The audit's derived program, now validated

The packet audit derived a seven-dword fragment program from the field
placements and marked it **UNVALIDATED**, with the obligation that
`i9xx_fragprog.c` reproduce it under a host test before it reached hardware.
It does, exactly:

```
7d050005  header, payload 6, length 5
190a3c00  dcl T8 (diffuse), channels xyzw
00000000  D1 must be zero
00000000  D2 must be zero
02203ca0  mov oC, T8
01230000  src0 swizzle .xyzw
00000000  A2: no src1, no src2
```

The builder was written from the field placements rather than from the derived
dwords, so this is two derivations agreeing, not a transcription checking
itself. **That obligation from the audit is discharged.** The other one - the
invariant-state residual - is still B2's to answer.

## The golden stream

59 dwords: 34 of state, 7 of fragment program, a 2-dword MI probe, then the
16-dword inline primitive. **Execution CRC `78780722`**, which step 4's
generator must place in the mini-VDD's arm table.

The CRC is asserted twice and deliberately not in the same way: once against
the golden array through `v9x_i9xx_crc32_dwords`, and once against a literal
computed independently. A self-consistent pair of computations would not catch
both drifting together, which is precisely what step 2 found had happened to
Phase 4's.

## Three decisions inside the units worth stating

**The float transport touches no `float` type.** Win16 display drivers run with
the FPU in whatever state the interrupted application left it, and Open
Watcom's 16-bit floating-point emulation would drag a library into an already
tight code segment. So the bit patterns are constructed by integer arithmetic,
exact below 2^24 - about twenty thousand times any coordinate this driver will
emit. Both directions exist because `INTEL3D0.TXT` reports vertices twice, as
raw bits and as decoded integers; the decoder half is what makes that
redundancy meaningful rather than decorative, and the stream decoder uses it to
bounds-check every coordinate.

**The refusals are numbered and each has its own reason**, including six
distinct float-transport reasons - negative, negative zero, not finite,
denormal, fractional, too large. The Phase 4 record names ambiguous refusals as
the reason that phase cost eight boots; "the vertex was wrong" is not a
diagnosis. The stream decoder additionally returns the **dword index** it
rejected, because a reason alone cannot distinguish which of the two
`BUF_INFO` packets was at fault.

**Two decoders, two allowlists.** `v9x_i9xx_decode_phase4_stream` is untouched,
and a test asserts a Phase 4 stream does not satisfy the Phase 5 decoder. The
phases exist to be distinguishable; one shared decoder would defeat that.

## The tests, and two defects they caught

26 single-dword mutations of the golden stream, each asserted to be rejected
with a specific reason **and** at a specific index. Plus an exhaustive float
round trip over every value a screen coordinate can take.

Two real defects surfaced, both mine:

- **The float exactness boundary was off by one.** `v9x_i9xx_float_to_int`
  refused an unbiased exponent of 23 as too large, but at 23 the mantissa's
  last bit is exactly the units place, so 2^24 - 1 is representable and must be
  accepted. The exhaustive round trip found it at `0x00ffffff`.
- **The decoder rejected its own golden stream**, because it had no cases for
  the invariant packets - AA, the three defaults, the coordinate-set bindings,
  the depth sub-rect disable. An allowlist that does not list everything the
  builder emits is not an allowlist.

One apparent third defect was a mistake in the test rather than the code:
`0x42f80000` is 124.0, an integer in range, not the fractional coordinate the
mutation intended. `0x42f10000` is 120.5 and refuses correctly.

## Part 1 was necessary, and this step proves it

The six new units add 5,085 bytes of 16-bit code. `I9XXCODE` went from 21,875
to 26,960 of its 57,344 budget; `_TEXT` is unchanged at 40,905.

Had the code segment not been split, the single segment would now be
**61,944 + 5,085 = 67,029 bytes against a 65,536 hard limit** - over by 1,493.
Phase 5 step 3 alone would have been unbuildable. The split was not
anticipatory tidiness.

The linker also caught a third `__U4M` crossing while building this: the stream
decoder advanced through vertices with `vertex * V9X_I9XX_VERTEX_DWORDS`, and
five is not a power of two. It now advances by a stride. That is the third time
`wlink` E2052 has caught a runtime-helper reference that no grep would have
found, and it is worth recording that the mechanism keeps earning its place.

## Gates run

- `build-host.ps1` and `build-host-msvc.ps1` - pass. Every new unit compiles
  clean under both, which is what keeps them host-testable.
- `build-win16-ddi-skeleton.ps1 -Family intel-gma` plus its audit - pass.
- `run-checks.ps1` - pass.

## Not tested, and what is deliberately absent

Nothing here has been booted. Every constant remains **documentation-derived
and unconfirmed on this machine**, and this step does not change that - it
proves the builders produce what the audit says the hardware wants, which is a
different claim from the hardware wanting it.

Deliberately absent: there is no executor, no sequencer, no capture writer and
no path by which any of this reaches the ring. `V9X_I9XX_PHASE5_EXECUTOR` does
not exist yet. These units are pure functions over arrays, and the driver does
not call one of them. That is step 6's job, behind its own guard, and the plan
is explicit that Phase 5 must be buildable dark first.
