# One USER import stops the display driver loading, silently

Filed: 2026-09-13
Status: fixed in source and guarded at build time; not yet confirmed on the
machine
Machine: MICHAEL-NETBOOK, HP Mini 110-1000, 945GSE / GMA 950, Windows 98 SE
booted direct from the live USB stick
Found in: `intel-gma` builds `ebaa1ed-dirty` and `c4f73e6`, the first two
Phase 4 first-write packages

Supersedes the cause given in
`2026-09-13-driverinit-systemini-write-stops-the-display-driver-loading.md`.
That record's fix is kept on its own merits; its diagnosis was wrong, and the
reasoning error is recorded below because it cost two boots.

## Symptom

The desktop came up at 640x480 in sixteen colours. Display Properties offered
the full mode list including 1024x576, and selecting it and rebooting still
produced 640. No Velocity9x diagnostic file was written: no `INTELRNG.TXT`,
no `INTELARM.TXT`, and `V9XBOOT.INI` kept its 12 September contents.

`V9XSYNC.INI` was written on each boot and reported the new build, because the
settings DLL runs from the `Run` key at logon and is an ordinary DLL.

## Cause

`src\display16\intel_exec16.c` contained

```c
request.safe_mode = (WORD)(GetSystemMetrics(67) != 0);
```

as the Safe Mode input to the arm contract. `GetSystemMetrics` lives in
**USER**. GDI loads the display driver during GDI initialisation, before USER
exists, so the NE loader could not resolve the import and refused the whole
module. `DriverInit` was never called. Windows then used the INF's fallback
row

```
HKR,"MODES\4\640,480",drv,,vga.drv
```

which is the 640x480 sixteen-colour desktop. The devnode itself started
normally, which is why the registry's mode list was intact and Display
Properties still offered 1024x576: those rows are the INF's, not the driver's.

Measured with `wdump -e` on the built packages:

| Family | Module reference table |
|---|---|
| intel-gma (broken) | DIBENG, KERNEL, **USER** |
| vbe, s3, ati | DIBENG, KERNEL |

The single USER relocation was ordinal 179, `GetSystemMetrics`.

## Fix

- The call is gone. Safe Mode forces the standard VGA driver, so a boot that
  reaches this code is already not a Safe Mode boot; the field stays in the
  arm contract for the host tests and for any future caller that can answer
  it without USER.
- **`scripts\audit-family-binary.ps1` now reads the NE module reference table
  and refuses any import other than KERNEL and DIBENG**, for every family, at
  package build time. Verified by reintroducing the call and watching the
  build fail with that message. This is the durable fix: the class of bug is
  invisible on the machine and obvious in the binary.
- `v9x_boot_trace` now flushes. See below.

## The reasoning error, recorded

Twice, `V9XBOOT.INI` keeping an old timestamp was read as proof that
`DriverInit` had not run. It is not proof. `v9x_boot_trace` wrote through
`WritePrivateProfileString` with no flush, so the `libmain` marker sat in
Windows' profile cache and reached disk only if the boot finished normally,
which is precisely the boot whose trace nobody needs. The first diagnosis
built on that and named the wrong cause.

The marker now flushes. A stale `V9XBOOT.INI` can be trusted to mean the
driver did not load, which is what it was always assumed to mean.

## Hypotheses killed

- "The arm transaction's SYSTEM.INI write stopped the load." It was a real
  hazard and is fixed, but it was not the cause: the module never loaded far
  enough to run any code.
- "The mini-VDD API v6 binary fails to load." Never reached.
- "Files were not deployed." All four binaries in `WINDOWS\SYSTEM` hash-match
  the package exactly, on both attempts.
- "The devnode did not start, like the 2026-09-12 function-1 install." It
  started; the offered mode list proves it.
- "The driver's code segment overflowed 64K." It is 62,370 bytes, under the
  limit, though only by about 3K.

## Still unknown

Whether the v6 mini-VDD loads, and whether the Phase 4 sequence runs. Three
boots have now been spent without reaching either question.

## Note for the next person

The driver's code segment is at 62,370 of 65,536 bytes. Phase 5 will need a
second code segment or a smaller driver.
