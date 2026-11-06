# GL_EXT_compiled_vertex_array on four machines, and OpenGL drawing nothing on the RS690

Date: 2026-11-06

Status: measured on four machines on `3faeae06` from a clean tree:
`Build=3faeae06` in every V9XGLP and V9XGLIDP report, every installed
file's SHA-256 matching the package. The four probes were rebuilt with
that build id first. The first run of `scripts/run-release-checks.ps1`
on several machines at once; the tables below are the record it wrote,
with its TODOs answered.

Evidence: [`../probe/0.38.0-cva-checks-2026-11-06/`](../probe/0.38.0-cva-checks-2026-11-06/),
one folder per machine.

The change is `GL_EXT_compiled_vertex_array` in the shared OpenGL ICD
(`3faeae06`): offered on every engine, the lock recorded, nothing
cached. V9XGLP's new `Cva` case checks the extension string, both
entry points, the extension's errors, the two queries, and a locked
`glDrawArrays` that must put green on the screen.

## Builds

All eight files by WININIT rename (no `NUL=` lines), each machine
restarted by `reset`, then every installed file's SHA-256 compared with
the package:

| Machine | Package | Boot | Installed |
|---|---|---|---|
| i5-4570, HD 4600 | intel-gma | 183 to 184 | 8 of 8 |
| Netbook, GMA 950 | intel-gma | 277 to 278 | 8 of 8 |
| A8U4I5, Rage 128 Pro | ati | 671 to 672 | 8 of 8 |
| M-GB8KRE, RS690 (remote help) | ati | 85 to 86 | 8 of 8 |

The run took 8.1 minutes in all: the HD 4600 finished in 1.8, the
GMA 950 in 2.9, the Rage 128 in 4.1 and the RS690, over the relay, in
8.1.

## Results

| Check | i5-4570, HD 4600 | Netbook, GMA 950 | A8U4I5, Rage 128 Pro | M-GB8KRE, RS690 (remote help) |
|---|---|---|---|---|
| V9XGLIDP | PASS failures=0 | PASS failures=0 | PASS failures=0 | FAIL failures=14 |
| V9XGLP | PASS | PASS | PASS | PASS; failing: Cva |
| `CvaResult` | PASS | PASS | PASS | FAIL (draw read black) |
| V9XR3DP | COMPLETE | COMPLETE | COMPLETE | COMPLETE |
| V9XD32 ddi5 | PASS | PASS | FAIL: T4Flat, T16 | |
| V9XD32 ddi6 | PASS | PASS | FAIL: T4Flat, T16 | |
| V9XD32 ddi7 | PASS | | | |
| V9XD32 default | PASS | | FAIL: T4Flat, T16 | PASS |

On all four, `CvaAdvertised=1`, `CvaEntryPoints=2`, `CvaErrorsOk=1`,
`CvaQueriesOk=1` and `ErrorAfterCva=0`: the extension's string, entry
points, errors and state are right everywhere. On the three that draw
OpenGL, `CvaLockedDraw=0x0000FF00`.

## What changed since 0.37.0

Compared with `0.37.0-release-checks-2026-11-06`, by machine and check:

- **HD 4600, GMA 950:** no verdict changed; `Cva` new, PASS.
- **Rage 128 Pro:** no verdict changed; `Cva` new, PASS. V9XD32's
  T4Flat and T16 fail as at 0.37.0, the probe interaction filed in
  `docs/issues/2026-11-06-v9xd32-t20-t21-break-t4-t16-on-rage128.md`.
  The 0.37.0 folder's extra run with the 0.36.0 probe was not repeated.
- **RS690:** not in that folder (its 0.37.0 checks are
  `2026-11-06-rs690-0.37.0-checks.md`). Against those: Glide's 14
  failures are the same, and `Cva` is new and fails - see below.

## OpenGL draws nothing on the RS690, and did at 0.37.0

The RS690's `CvaLockedDraw` read back the clear colour, black. So did
every other drawing check in the same V9XGLP report: `OffsetNoneOk=0`,
`OffsetNearerOk=0`, `FogLinearOk=0`, `FogExpOk=0`. The renderer string
is the generic `Velocity9x`, not a chip name. 0.37.0's report for this
machine (`../probe/rs690-0.37.0-checks-2026-11-06/1024x768x16-V9XGLP.INI`,
`e5f3a34f`) already has `OffsetNoneOk=0` and `FogLinearOk=0`. So the
extension did not cause this; its draw check is the first verdict line
that shows it.

This disputes the 0.37.0 RS690 record, which says "OpenGL is
unaffected" by the render-interface refusal that leaves Glide and 3DCIF
drawing nothing. That claim rested on V9XGLP's `Result=PASS`, which
does not count the probe's drawing sub-checks. The likely cause is the
same as Glide's: the ICD draws through the HAL's render interface, the
RS690 map refuses those draws, and the chip has no software fallback.
Not measured further here.

The RIVA 128ZX shows the same pattern: `OffsetNoneOk=0`,
`FogLinearOk=0`, generic renderer, at 0.35.0 and 0.37.0.

## Not covered

- The 3dfx, matrox, nvidia, s3, sis and vbe packages: no machine
  booted their files. The RIVA 128ZX (desktop not ready) and M-JJN1ZZ
  (offline since an install's reset earlier the same day) were not
  run.
- Quake 3: whether it calls `glLockArraysEXT` here (the ICD logs the
  first call to `V9XGL.LOG`) was not run.
- 3DMark: not run (no `-ThreeDMark`).
