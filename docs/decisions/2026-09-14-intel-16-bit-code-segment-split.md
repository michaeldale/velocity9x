# Split the Intel family's 16-bit code across two segments

**Date:** 2026-09-14
**Machine:** development host only. No guest, no hardware. Every number below
comes from Open Watcom 2.0 beta (Jul 21 2026) on the checked-in tree.
**Plan:** `docs/plans/intel-gma950-phase5.md`, Part 1.

## Why

The `intel-gma` family had 2,126 bytes of Win16 code headroom and nothing in
the tree watching it. The 64 KiB limit is per segment, and one ordinary Phase 5
step - at the 2 KiB-per-step review rule in
`docs/plans/multi-chip-restructure.md:135` - exhausts it. `audit-family-binary.ps1`
asserted a DGROUP budget but parsed no `_TEXT` row, and `golden-baseline.ps1`
tracked the s3 and mga2 maps but not the one at risk.

## Result

Measured on the linked image, `BuildId=p5base`:

| Segment | NE frame | Bytes | Headroom to 65,536 |
|---|---|---|---|
| `_TEXT` | 0001 | 40,901 | 24,635 |
| `I9XXCODE` | 0003 | 21,329 | 44,207 |

`wdump -s` reports **three** segment-table entries (two CODE, one DATA), both
CODE rows `CODE|FIXED|SHARE|PRELOAD|EXECREAD|RELOCS`.

Before the split, one CODE segment of 61,944 bytes. The two segments total
62,228, so the boundary cost 284 bytes net - the far calls, the bridge and the
string helpers, less the `clibc.lib` string objects that dropped out of the
link.

Eleven units moved: `i9xx_mmio`, `i9xx_gtt`, `i9xx_ring`, `i9xx_arm`,
`intel_diag16`, `intel_gtt16`, `intel_event16`, `intel_ring16`,
`intel_exec16`, `intel_boot16`, and the new `intel_str16`. `intel_hw16.c` and
`gma950_hw16.c` stayed, because they own the near `V9X_HW16_OPS` /
`V9X_HW16_DEVICE` tables.

## Two things the plan got wrong

Both were found by measurement, and both make the change safer than planned.

### The class must differ; `-nt=` alone is not a split

The plan said, in bold, **"Do not pass `-nc=`"** - keep the class `CODE` so the
map rows and the auditor's class-based parsing keep working. Built that way,
the map does gain a second row:

```
_TEXT                  CODE           AUTO           0001:0000       00009fc5
I9XXCODE               CODE           AUTO           0001:9fc5       0000534f
```

and it is an illusion. Both rows are at frame `0001`, and `wdump -s` reports
`number of entries in segment table = 0002H`. `wlink` combines same-class
segments into one physical NE segment, so the image was never split and the
64 KiB limit still applied to the sum. Passing `-nc=I9XXCODE` as well produces
the real third entry recorded above. The cost is that the map's Class column
now reads `I9XXCODE` rather than `CODE`, which is why the new auditor gate
derives its expected segment set from the manifest instead of matching on class.

### A missed far declaration is a link error, not a silent crash

The plan's central risk argument was that "a near call cannot cross a segment -
it is a **silent crash**, not a link error", and the near-call `wdis` gate
(gate b) existed as the backstop for exactly that.

With distinct classes it is a link error. Two declarations were missed while
implementing this, and `wlink` refused both:

```
Error! E2052: file ...\intel_ring16.obj: relocation at 0003:3399 not in the
same segment
```

The first was `v9x_get_build_identity` in `intel_ring16.c:93`, which the
planning grep had not found. The linker named the object; `wdis -a` on it named
the symbol.

**Gate (b) was therefore not implemented, and this is a deliberate deviation
from the plan.** A gate that can only fire after a link that cannot succeed is
dead code. What the linker does *not* catch is a declaration that disagrees
with the definition - that compiles to a far call to a near entry point with no
complaint - so the assertion was spent there instead, in `check-tree.ps1`:

- no bare `extern` anywhere for a symbol `intel16.h` declares;
- no definition of such a symbol without `V9X_I9XX_FAR`.

Both were verified to fire by breaking them deliberately.

## The C-runtime rewrites

`clibc.lib` declares segment `_TEXT` and there is no linker mechanism to place
a second copy in `I9XXCODE`, so the moved units must not reference it.

- `i9xx_gtt.c` - the FNV-1a prime multiply became the shift-add decomposition
  of 16777619 = 2^24 + 2^8 + 2^7 + 2^4 + 2^1 + 1. Exact, not approximate:
  modular arithmetic distributes over the sum.
- `i9xx_ring.c` - one `static v9x_i9xx_mul32` shift-add loop for the two
  `(height - 1) * pitch` bounds calculations.
- `intel_exec16.c` / `intel_boot16.c` - `<string.h>` dropped for the four
  `v9x_intel_str_*` functions in the new `intel_str16.c`.

Equivalence is proved by the existing `test_i9xx_gtt.c` and `test_i9xx_ring.c`,
which pass unchanged under both Open Watcom and MSVC. In particular the GTT
hash expectations are unchanged, which is the regression test that matters on
the netbook: `INTELGTT.TXT`'s hash must still equal the 2026-09-12 capture.

Verified afterwards with `wdis -a`, as the plan required: no moved object
references `__U4M`, `__U4D`, `__I4M`, `strcmp_`, `strcpy_`, `strlen_` or
`strcat_`, and the only calls in `i9xx_gtt.obj` and `i9xx_ring.obj` are to
their own statics - Watcom inlined every constant 32-bit shift rather than
calling a shift helper. `__U4M` remains in `_TEXT`, where the seven staying
units still use it.

## The crossing surface, as built

Into `I9XXCODE`: `v9x_intel_publish_{mmio_fingerprint,gtt_inventory,ring_plan,event}`,
`v9x_intel_boot_arm_prepare`, `v9x_i9xx_sandbox_calculate`.

Out of `I9XXCODE`: `v9x_get_build_identity` and `v9x_selected_mode_geometry`,
both shared with every family, both reached through far wrappers in the new
`src/display16/intel_bridge16.c`.

One function pointer needed a forwarder: `intel_hw16.c` stored
`v9x_intel_publish_event` directly in the near `V9X_HW16_OPS.publish_event`
slot. It now stores a `static` near forwarder that makes the far call. Audited
every table in the tree; this was the only break, and no moved function is an
NE export or has its address taken by Windows.

`intel_bridge16.c` does not include `<windows.h>`, contrary to the first draft
of it: it calls no Win16 API, and the compact model already addresses all data
far, so `ddi.c`'s `WORD FAR *` parameters are spelled `unsigned short *` here
and are the identical type. That keeps it off `check-tree.ps1`'s OS-boundary
list, which it has no business being on.

## The three new gates

None of this existed before.

1. **CODE-segment budget**, in `audit-family-binary.ps1` beside the DGROUP one.
   Every family now logs its code occupancy on every build and fails over
   57,344 bytes - 8 KiB below the hard limit, four maximum-sized review steps.
   The expected segment set comes from the manifest and the **count is
   asserted**, so a `CodeSegment` that never reached the compiler is caught
   here rather than by a link failure three steps later.
2. **Per-segment attributes.** The old check matched one
   `CODE|FIXED|SHARE|PRELOAD` line anywhere in the dump, which with two CODE
   segments passes if either is right. It now iterates every CODE row.
3. **Source-level boundary rules** in `check-tree.ps1`, described above.

All four assertions (1, 2, and both halves of 3) were verified to fire by
deliberately breaking each one.

## Gates run

- `check-tree.ps1` - pass.
- `build-host.ps1` and `build-host-msvc.ps1` - pass. `V9X_I9XX_FAR` vanishes
  under both, which is what keeps the `i9xx_*` units host-testable.
- `build-win16-ddi-skeleton.ps1` + `audit-family-binary.ps1` for all five
  families - pass. Occupancy: intel-gma `_TEXT` 40,901 / `I9XXCODE` 21,329;
  s3 42,229; vbe 40,037; ati 39,667; matrox-m2 34,685.
- `run-checks.ps1` - pass.
- **Inertness, measured.** Built s3, vbe, ati and matrox-m2 at a pinned
  `BuildId` with the change present (A), at pristine `HEAD` (B), and with the
  change present again (C). A = B byte for byte for all four, and A = C, so the
  link is reproducible and the shared change is inert. A first, uncontrolled
  attempt at this comparison reported a difference; it was reading stale
  artefacts and is superseded by the A/B/A run.

## Known, and not caused by this change

`golden-baseline.ps1 -Compare` fails with 214 differing entries. Verified
pre-existing by stashing the change and re-running against pristine `HEAD`:
identical failure. The archive at `../velocity9x-golden/golden-compare/` was
last written 2026-08-16, before the multi-chip restructure, and still references
`build/win98se-active`, which no longer exists. Re-baselining it is a separate
decision. The intel-gma map was added to `$trackedMaps` so that whenever it is
re-baselined, the map that was actually at risk is covered.

## Not tested

Nothing here has run on hardware or in a guest. The claim that Win98's NE
loader loads, fixes up and keeps `FIXED` a second code segment in this driver
is **untested**; it is the largest remaining risk in Part 1, and the plan's
cheap experiment for it - giving the s3 family a `CodeSegment` on one leaf unit
on a throwaway branch and running `run-family-enable-gate.ps1` against the S3
guest - has not been run. The netbook regression boot described in the plan has
not been run either.
