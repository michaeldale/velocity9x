# V9XTRACE hung rewriting a snapshot section over 32 KiB

Date: 2026-10-21. Machine: TOMMOHP (GM965), boots 31-33. Evidence:
[`../probe/tommohp-v9xtrace-reuse-2026-10-21/`](../probe/tommohp-v9xtrace-reuse-2026-10-21/).

## Symptom

From 10:13 that day V9XTRACE.EXE never finished on TOMMOHP:

- A plain v9xctl exec timed out after 60 s.
- Run with `-Detach`, it wrote nothing new: `C:\V9XDIAG\V9XSNA7.INI` kept
  `DumpTime=2026-10-21 10:13:50`, so every "snapshot" fetched after that
  was the same old one. The GM965 decision record discarded those copies
  as stale.

## What it was

V9XTRACE takes the first free name of eight, `V9XSNAP.INI` then
`V9XSNA1.INI` to `V9XSNA7.INI`, and reuses the last once all are taken.
The 10:13 run, after a 3DMark 2000 benchmark had filled the HAL's trace
ring, was the first to take `V9XSNA7.INI`. Its `[Velocity9xTrace]`
section came to **32,787 bytes** (1,326 lines).

Every later run reused that file, and the first thing a run does is
delete its own section through `WritePrivateProfileStringA`. That call
never returned:

- A stage file the tool now writes with plain `WriteFile`
  (`C:\V9XDIAG\V9XTRSTG.TXT`) read `Stage=start` and nothing after
  (boot 32). `start` is written just before that delete.
- This was the first V9XTRACE process of the boot, so no earlier run was
  holding the file.
- Windows 98 caches private-profile writes and flushes them when the
  process ends. A process that never ends leaves the file on disk as it
  was, which is why the old DumpTime looked like a fresh dump.

| Section that run started from | Bytes | Result |
|---|---|---|
| `V9XSNA7.INI` from 10:13 | 32,787 | the delete never returned (boots 31-32) |
| `V9XSNA2.INI` | 31,349 | written and read normally |
| boot 33 files | about 20,500 | the old and the fixed tool both finished |

The line falls at 32,767, a signed 16-bit size, and Windows 98's profile
API is 16-bit underneath. That fits, but it rests on one file that hung
against several that did not. No section between 31,349 and 32,787 bytes
was tried, so the threshold is consistent with the evidence, not
measured. The file itself was clean: no NUL bytes, seven sections, no
line longer than 179 characters.

## Fix

`v9x_result_file` now truncates the reused file with
`CreateFile(CREATE_ALWAYS)` before any profile write. That way no run
rewrites an existing section, and every run starts from an empty file,
the case that has always worked. Truncating is not a delete, so it stays
within the rule for remote machines (no deletes through the agent).

The stage file stays, for the next time the INI cannot say how far a run
got.

## Measured (boot 33, fresh)

- **The fixed tool, twice in a row:** exit 0 in 251 ms and 183 ms.
  - Stage `done` both times.
  - `V9XSNA7.INI` was rewritten each time (DumpTime 13:37:09, then
    13:37:20, `SnapshotReused=1`).
- **The old tool, after them:** finished in 159 ms (13:37:41). By then
  the file it reused was a 20 KB one, which is what clears it.

## Not covered

A single run whose own trace section passes 32 KiB was not tried. The
10:13 run wrote 32,787 bytes by appending and completed, so appending
past the limit appears to work; only rewriting does not.
