# Velocity9x — working agreement

A from-scratch Windows 9x display driver: a 16-bit Win16 driver, a 32-bit flat
DirectDraw/Direct3D HAL, and a mini-VDD, built with Open Watcom against the
Windows 98 DDK and driven by PowerShell scripts in `scripts/`.

Read `docs/specifications/win9x-driver-boundaries.md` before changing anything
that crosses a module boundary.

## Verify before you claim

- `./scripts/check-tree.ps1` — structure and cross-file contract assertions.
  No compiler needed. Cheap; run it often.
- `./scripts/build-host.ps1` — host-side unit tests. First thing after
  touching a family manifest or any pure-logic module.
- `./scripts/run-checks.ps1` — the full local gate. Must be green before a
  commit that changes code.

Never report a change as working on the strength of the code reading
correctly. Either a gate passed, or a guest/machine produced evidence, or say
plainly that it is untested.

## Hardware claims need measurements

This project's expensive mistakes have all been confident statements about
silicon that nobody measured. A claim about what a chip, BIOS or guest does
belongs in `docs/decisions/YYYY-MM-DD-<topic>.md` with the log, INI or capture
that produced it — including the hypotheses the evidence killed. A bug found
on real hardware is filed in `docs/issues/`; a multi-session effort is planned
in `docs/plans/`.

Where a defect can be reproduced in a host test, write the failing test first,
watch it fail, then fix it. Most cannot: for those, the probe-and-record loop
above is the equivalent, and the decision doc is the artefact.

## C

C89, Open Watcom, freestanding-ish. The 16-bit driver and the 32-bit HAL share
headers, so portability is an ABI constraint, not a preference.

- No `enum`. There are none in the tree, deliberately: size is
  implementation-defined and these modules straddle two memory models. Use the
  fixed-width `v9x_*` types and the `V9X_TRUE` / `V9X_FALSE` constants.
- Everything not exported is `static`. Making a symbol external is a design
  change — say so and get agreement first.
- `v9x_<vendor>_<chip>_<verb>` naming. The prefix is worth its length; do not
  shorten a name to hit a character count.
- Declarations at the top of the block. Braces on every `if`, including
  one-liners.
- Early return over nesting. Blank lines between logical blocks.
- Comments say *what* the block does and *why* it is done that way —
  especially why the obvious alternative is wrong. Register-level code cites
  the databook or spec that licenses it.
- Extract spec-derived and recurring values into named constants. Leave
  self-explanatory one-offs inline.

## Layering

Hardware mechanics live behind a backend; policy lives in host-testable C.
`src/common/mtrr.c` is the pattern: the mini-VDD reads the MSRs and interprets
nothing, the decision is pure arithmetic with a unit test, and `enable16.c`
applies it. Do not reach past a layer — no register poke from display code, no
policy in a VxD.

## Scope

Change what the task requires and no more. Do not reformat, re-comment or
"improve" a block you did not otherwise touch; the diff is the unit of review.

## Commit messages

The repository's convention, which is not the 50-character one:

- Subject in the imperative, capitalized, no trailing period, under ~80
  characters. It states the finding, not the file list — "Measure where the
  Trio64 fill goes: a validated probe, and three dead hypotheses", not
  "Update trio64.c".
- Blank line, then a body wrapped at 72 columns.
- The body explains context and reasoning, names what was measured and on
  which machine, and says which gates were run. Record what the evidence
  disputes, not only what it confirms.

## Prose

Few words, chosen carefully. No superlatives, no praise, no restating the
request back. When something is uncertain or was not checked, say so.
