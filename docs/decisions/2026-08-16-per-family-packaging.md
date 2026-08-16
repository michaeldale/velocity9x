# Per-family packaging

Date: 2026-08-16
Status: accepted

## Context

Velocity9x built one package per exact chip: `-S3Trio64` and
`-MatroxMillennium2` switches selected `#ifdef`-guarded code in `runtime.asm`,
`ddi.c` and `dd16.c`, and the post-link audit asserted that exactly one chip's
instructions were present. Adding a chip meant editing four chokepoints plus
every build script, and the audit's "one chip per binary" assumption made a
shared binary impossible.

`docs\plans\multi-chip-restructure.md` needs a packaging unit that can hold
several chips. Three shapes were available.

## Options

**One universal binary.** Every chip in one driver, dispatching on PCI ID at
run time. Rejected: the Win16 driver builds `wcc -mc` into a single 64 KiB code
segment, and every chip added would eat that budget for machines that can never
use it.

**One build per exact chip (status quo).** Rejected: it does not scale past a
handful of chips, and the per-chip audit cannot express "these two chips
legitimately share this binary".

**One package per chip family, runtime PCI dispatch within the family.**
Accepted.

## Decision

A family is one built package covering one or more chips that share a driver
binary. `packaging\families\<id>\family.psd1` declares the chips, sources,
defines, audit signatures, INF metadata, floppy placement and VM profile; the
build scripts read it rather than hard-coding chip facts. See
`docs\specifications\family-manifest.md`.

Consequences that follow from the choice:

- **Audits become relative, not absolute.** A family image must match all of
  its own chips' signatures and none of any other family's. The forbidden set
  is derived from the sibling manifests, so adding a family strengthens every
  existing family's audit with no script change.
- **The INF becomes generated.** A multi-chip family needs one model line and
  one install section per chip with per-chip `MODES`, which the old
  string-replacement rewrite of a checked-in single-model INF could not
  express. The single-hardware-ID assertion becomes set equality against the
  manifest.
- **A PCI ID belongs to exactly one family.** Otherwise Windows sees two
  matching models and which driver installs is a coin toss.
- **Families with no emulator are declarable.** `Vm.Emulator = 'none'` makes
  the VM runner refuse with a real-hardware-only error instead of silently
  testing the wrong guest.

## Status of the migration

`s3-virge`, `s3-trio64` and `matrox-m2` are separate single-chip families that
encode exactly what the old switches did, so the packages stay byte-identical
while the source restructure proceeds. Phase 8 of the plan merges the two S3
manifests into one two-chip `s3` family and retires the legacy switches,
`LegacyOutputName`, and the checked-in `packaging\win98se\velocity9x.inf`.
