# Mini-VDD API v7: a read-only reserve hash that ships unarmed

**Date:** 2026-09-15
**Status:** Step 5 of `docs/plans/intel-gma950-phase5.md` Part 2, complete on
the host. **Nothing has been booted.**

## What it is, and why it is read-only

Phase 5's whole result is a read-back: the GPU draws a triangle into the
reserve and the driver has to report what landed there. Doing that through the
existing `V9XMINI_FN_I9XX_RING_MEMORY` verb would be 153,600 far calls for a
640x480x16 target, so v7 adds `V9XMINI_FN_I9XX_RING_HASH` (0015h), which hashes
a bounded dword range in one call.

It is **read-only**, and that is the design rather than a property it happens
to have. Because it never writes, it can ship in the unarmed build — behind
`V9X_INTEL_MMIO_FINGERPRINT`, not `V9X_I9XX_FIRST_WRITE_EXECUTOR` — so B1, the
unarmed boot, proves the hash path before any armed boot depends on it to
report the triangle. A verification instrument that first runs on the boot it
is verifying is not an instrument.

`check-tree.ps1` asserts all three of those properties: exactly one read
through the walked pointer and no store through it, exactly two call sites, and
the fingerprint guard rather than the executor guard. All three were verified
to fire by deliberately breaking them.

## Two passes, both returned

The verb hashes the same bytes twice and returns both results, in EBX and ECX.
It does **not** compare them.

That is deliberate and follows the rule the GTT capture already set: an
unstable read has to reach the caller as two different numbers, not hidden
behind one boolean. Collapsing it would discard exactly the failure the verb
exists to detect — memory that reads differently on two passes is the signature
of a mapping that is not what we think it is, and the capture must be able to
show it.

`V9xMini_I9xx_Hash_Dword` (FNV-1a) is reused unchanged; the new
`V9xMini_I9xx_Hash_Range` only walks a range and calls it.

## Bounds, each with its own number

Seven refusals, each distinct, returned in EDX:

| # | Refusal |
|---|---|
| 1 | no Phase 1 fingerprint capture, so the device is unidentified |
| 2 | offset not dword aligned |
| 3 | dword count is zero |
| 4 | count above the per-call bound |
| 5 | offset + length wrapped |
| 6 | range extends past the reserve |
| 7 | `_MapPhysToLinear` refused the reserve |

**Refusal 5 is checked on carry, not by a signed compare**, and the plan was
right to call that out specifically. The byte length is `count << 2` and the
end is `offset + length`; both are tested with `jc`. A signed comparison would
accept a count whose byte length wrapped past 2 GiB and then read wherever that
landed — inside a VxD, with no memory protection worth the name.

## Its own mapping, deliberately

The hash keeps `V9xI9xxHashLinear`, separate from the staging path's
`V9xI9xxRingLinear`, for two reasons. It must work in a build with no executor
compiled in at all, where the staging mapping never exists. And a mapping that
is never used to write cannot be confused with one that is — the separation is
the argument that this verb cannot damage anything.

## One literal removed that was not asked for

The staging path spelled the reserve's physical address as `0d06b0000h` and its
size as `00100000h`, both hand-typed at step 2. Adding a second mapping would
have made two hand-typed copies of the same address, which is the drift step 4
just finished removing.

So `loader.asm` now composes it once:

```
V9X_I9XX_GMADR_BASE          EQU 0d0000000h
V9X_I9XX_RESERVE_PHYS        EQU V9X_I9XX_GMADR_BASE + V9X_I9XX_RESERVE_OFFSET
V9X_I9XX_RESERVE_BYTES_TOTAL EQU 000100000h
```

`V9X_I9XX_RESERVE_OFFSET` is generated from the C builders. The GMADR base is a
PCI BAR value measured on the one tested machine and cannot come from them, so
it stays a literal — but the two halves can no longer drift apart, and both the
staging path and the hash path now use the composed constant.

## The contract, and the 16-bit side

`V9XMINI_API_VERSION` is v7, with the matching `V9X_VBE_API_V7` on the C side;
`check-tree.ps1` asserts the two agree, as it does for every version before it.
The package pair remains exact-match, so a v6 mini-VDD and a v7 driver refuse
each other rather than half-working.

`runtime.asm` gains `V9xMiniI9xxRingHash`, which returns the two passes and the
refusal reason through three globals rather than registers — a Win16 PASCAL
function has one return value and three numbers matter here.

## Gates run

- `check-tree.ps1` — pass, including the three new assertions, each verified to
  fire.
- `build-minivdd-skeleton.ps1` for intel-gma **and** s3 — the generated
  constants are included unconditionally, so a family without the fingerprint
  guard must still assemble, and does.
- `build-win16-ddi-skeleton.ps1 -Family intel-gma` — pass. `_TEXT` 42,463 and
  `I9XXCODE` 27,495, both well under the 57,344 budget. The thunk lands in
  `_TEXT` because `runtime.obj` declares no `CodeSegment`.
- `build-host.ps1` and `run-checks.ps1` — pass.

## Not tested

Nothing has been booted, and this step adds a specific kind of untested surface
worth naming: **the verb has never been called.** There is no caller at all —
the 16-bit thunk exists and the driver links it, but nothing in `intel_ring16.c`
or `intel_exec16.c` invokes it. The sequencer that uses it is step 6.

So what is proven here is that the code assembles, sits behind the right guard,
and satisfies three structural assertions. Whether `_MapPhysToLinear` returns a
usable pointer for this range on the netbook, and whether two passes over it
agree, is exactly what B1 is for.
