# Intel prerequisite: keep S3 DPMS writes out of non-S3 mini-VDDs

## Decision

`V9xMini_Set_Dpms` is now a positive, build-time S3 feature. Only
`build-minivdd-skeleton.ps1 -Family s3` defines `V9X_S3_DPMS`; every other
family assembles the helper as a bare `ret`. The existing `-NoDpms`
differential switch still suppresses the body in an S3 build.

This closes the prerequisite in
`docs/plans/hardware-d3d-on-intel-gma950.md`. The generic VBE package is the
route onto the Intel GMA 950, and it can no longer contain the writes that
unlock S3 extended sequencer registers, update SR0D, or clear CR56.

A positive guard is intentional. A newly added family gets the safe no-op
without having to know that it must opt out of another vendor's register path.
The build emits `V9X-MINI dpms-guarded family=<id>` in guarded images so a
serial capture names the policy in force.

## Host evidence

Built with MASM 6.11d and the Windows 98 DDK on 2026-09-12. Adjacent map
symbols give the assembled size of `V9xMini_Set_Dpms`:

| Build | Start | Next symbol | Size |
|---|---:|---:|---:|
| `-Family vbe` | `0000106A` | `0000106B` | 1 byte |
| `-Family s3` | `00001032` | `000010D8` | 166 bytes |
| `-Family s3 -NoDpms` | `0000108E` | `0000108F` | 1 byte |

The one-byte variants are the bare near `ret` in the guarded branch. The
normal S3 build retains the existing register implementation.

`scripts/check-tree.ps1` also fixes the source-level contract: the no-op must
be the first branch of `IFNDEF V9X_S3_DPMS`, the build script may define that
symbol only from the exact `s3` family selection, and the obsolete negative
`V9X_NO_DPMS` guard is forbidden.

## Verification

- `scripts/check-tree.ps1`: passed.
- `scripts/build-minivdd-skeleton.ps1 -Family vbe`: passed.
- `scripts/build-minivdd-skeleton.ps1 -Family s3`: passed.
- `scripts/build-minivdd-skeleton.ps1 -Family s3 -NoDpms`: passed.
- `scripts/build-host.ps1`: passed.

No Intel register is read or written by this change. Phase 1 remains the first
Intel-specific hardware work.
