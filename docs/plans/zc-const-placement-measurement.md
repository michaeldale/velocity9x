# The `-zc` measurement

Deferred from `intel-gma950-phase5.md`, which deliberately kept it out of the
segment-split diff. Nothing depends on it; it is an optimisation with a
plausible case against it, and the point of the exercise is to replace the
plausible case with four numbers per family.

## What `-zc` does

It is one flag on the shared Open Watcom argument line in
`scripts\build-win16-ddi-skeleton.ps1`:

```
-bt=windows -mc -zu -zc -zls -s -zq -wx -we
```

`-zc` places **all** const data in the code segment rather than in `DGROUP`.
That is why `CONST` reads `00000000` in every family's linker map — there is
nothing in it, because every string literal and const table has been put in
`_TEXT` instead.

It is on the shared line, so it is global by construction. There is no
per-family or per-file version of this decision.

## Why it is worth measuring

`_TEXT` is the segment the Intel family ran out of. `intel_exec16.c` alone
carries 121 string literals, and every one of them is sitting in the segment
whose 64 KiB limit forced the `I9XXCODE` split.

Dropping `-zc` would move all of that into `DGROUP`, which for Intel is
11,032 bytes plus a 1,024-byte heap against a 32,768-byte budget — roughly
20.7 KiB of room to receive them.

## Why it is probably still the wrong trade

Recorded here so the measurement is a check on the reasoning rather than a
substitute for it:

- It trades a segment that has just gained headroom for one whose hard limit
  also has to hold the stack. `DGROUP` running out is a stack overflow, which
  is a far worse failure than a link error.
- It changes all five families and forces an s3 and mga2 golden re-baseline.
- It does not remove the structural problem. The Intel family grows again next
  phase and the split has to happen regardless, so this buys time, not a
  different architecture.

**Recommendation, to be confirmed or killed by the numbers: measure it, record
the four figures per family, and keep `-zc` on.**

## How to measure

On a throwaway branch, on one pinned `BuildId` so the comparison is not
contaminated by an identity string of a different length:

1. Delete `-zc` from the argument line in
   `scripts\build-win16-ddi-skeleton.ps1`.
2. Build every family.
3. For each of the five, read `_TEXT`, `CONST`, `CONST2` and `DGROUP` from
   `build\win16-ddi-<family>\v9xdisp.map`.
4. Compare against the same four numbers with `-zc` on.

Twenty numbers, before and after.

## What to do with the result

Write them into `docs\decisions\YYYY-MM-DD-zc-const-placement.md` with the
recommendation confirmed or killed, including whichever hypothesis the numbers
disprove. Then throw the branch away — this is a measurement, not a change.

If the numbers do argue for dropping `-zc`, that is a separate diff of its own,
with the s3 and mga2 re-baseline in it and nothing else.

## Status

Not started. No numbers exist. Nothing in the tree depends on the answer.
