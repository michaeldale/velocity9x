# 86Box does not reproduce the netbook lock, in any software combination

Decided by: measurement, 2026-09-15. Guest `Win86SE`, 86Box 6.0, Windows 98 SE.

## What was claimed

The netbook hard-locked on the first unarmed boot of build `9e801a2`, and the
evidence pointed at the first far call from `_TEXT` into `I9XXCODE`:

- `V9XBOOT.INI` held `Stage=libmain` and nothing later. That marker is the last
  statement of `v9x_display_boot_log`.
- The very next statement in `DriverInit` is `v9x_intel_boot_arm_prepare()`,
  which the map places at `0003:6c18` while `DriverInit` is at `0001:3fda`.
- `INTELARM.TXT` still held exactly what `V9XCOPY` writes when it resets the
  arm state, so `arm_prepare` appeared not to have reached its first write.
- The machine's last successful boot was build `1f386d0`, twenty-five commits
  earlier, before the segment split existed.

The hypothesis was that the second code segment does not work on this machine:
the first time it was asked to make that crossing, it locked.

## What was measured

The actual `intel-gma` `V9XDISP.DRV` from `9e801a2` was installed into the
86Box S3 guest as `V9XINTEL.DRV`, with `[boot] display.drv` pointed at it. That
guest has a ViRGE/DX and no Intel GMA, but `DriverInit` and `arm_prepare` run
before any hardware validation, so the crossing is exercised in full.

Result: `Stage=fail-hardware-present`. The driver ran past `libmain`, through
the inbound far call, through `V9xEnsureDiagDir` and back, out of
`arm_prepare`, and on to hardware validation - which correctly refused, there
being no Intel device.

Both `C:\V9XDIAG\INTELARM.TXT` and `C:\V9XDIAG\V9XBOOT.INI` were then deleted
and the guest rebooted. Both were recreated, `INTELARM.TXT` at 37 bytes - the
same size as the netbook's, and the same content `arm_prepare` writes as its
first action. That second boot is what makes this evidence rather than
inference: the file was demonstrably written by the boot under test.

Two further combinations were then tried, because the first test kept the
guest's own s3 `V9XMINI.VXD` and the mini-VDD was the largest untested
difference:

| Guest configuration | Result |
|---|---|
| Intel `V9XDISP.DRV`, s3 mini-VDD | `Stage=fail-hardware-present` |
| s3 driver, Intel `V9XMINI.VXD` | `Stage=enable-ok`, full desktop |
| Intel driver **and** Intel mini-VDD | `Stage=fail-hardware-present` |

The Intel mini-VDD is the half that now carries the generated Phase 5 table
inside its data segment, and it loaded and ran to a full desktop. The pair
together reached hardware validation and refused there, with `INTELARM.TXT`
recreated at 37 bytes again.

**86Box does not reproduce the netbook lock in any combination available
here.**

## What this kills

**The segment split is not inherently broken on Windows 98.** The inbound far
call into a second `FIXED|PRELOAD|EXECREAD` code segment, the outbound far call
from it into `runtime.asm`, and `arm_prepare`'s own body all executed on a real
Win98 SE guest using the exact binary that locked the netbook.

It also kills the weaker form of the claim, that some specific thing in
`arm_prepare` - the string literals now living in `I9XXCODE` under `-zc`, the
`WritePrivateProfileString` calls made from that segment, the `V9xEnsureDiagDir`
crossing - is at fault. All of them ran here.

## What it does not establish

86Box is not the netbook. This does not prove the netbook's crossing works; it
proves the mechanism is sound and that the fault needs a cause the guest does
not reproduce. Candidates not excluded:

- Real Intel hardware. This is now the only variable left that the guest cannot
  supply: every software combination of the two Intel binaries has been tried
  here and none locked. Note that the guest always stops at
  `fail-hardware-present`, so nothing downstream of a *successful* hardware
  validation has been exercised at all - and on the netbook that check passes.
- Anything else among the twenty-five commits since `1f386d0`, of which the
  split is only one.

A narrowing that does come out of this. `query-start` is written from
`v9x_fill_gdi_info`, which GDI calls into `Enable`, not from `DriverInit`. The
netbook's `Stage=libmain` therefore means GDI never called `Enable` at all. And
`DriverInit` does very little after the `libmain` marker: `arm_prepare`,
`v9x_get_build_identity`, `v9x_log_init`, and `v9x_display16_start`, the last
of which sets three struct fields and emits a log record. It touches no
hardware and no VxD.

The netbook's own `Stage=libmain` reading also still rests on an unverified
assumption: that Win9x's `WritePrivateProfileString` rewrites a file when the
value is unchanged. If it skips such a write, `arm_prepare` may have completed
there too and the fault lies later. The `arm-pre` / `arm-in` / `arm-dir` /
`arm-post` markers added in this same change remove that assumption from the
next netbook boot.

## The assumption was false, measured 2026-09-15

Netbook capture `intel28`. `V9XBOOT.INI` was written at 17:00 with
`Stage=arm-post`, so `v9x_intel_boot_arm_prepare` ran to completion on that
boot. `INTELARM.TXT` in the same directory is still timestamped **16:42** -
two boots earlier - and `arm_prepare`'s first action is writing
`IntelEnableThisBoot=0` into it.

So Win9x's `WritePrivateProfileString` does **not** touch the file when the
value it would write is already there. The value was already `0`, and the
timestamp did not move.

That retroactively invalidates the original diagnosis in "What was claimed"
above. `INTELARM.TXT` being unchanged after the first locked boot was read as
`arm_prepare` never reaching its first write; it proves nothing of the kind,
and never did. The conclusion drawn from it - that the fault lay at the segment
crossing - sent three netbook boots after the wrong thing.

The lesson is narrower than "do not infer from timestamps": it is that an
absent write is not evidence unless the write was known to change something.
A marker whose value differs each time it is written, which is what the `Stage`
key is, does not have this failure mode.

## The display fallback latch, same day

`SYSTEM.INI` from capture `intel27` carries `*DisplayFallback=1` under
`[boot]`. The healthy 86Box guest carries `*DisplayFallback=0`.

With that latched, Windows loads the driver, runs `DriverInit`, and then
declines to call `Enable` at all - which is precisely the
`Stage=arm-post` / `DriverInitCall=1` / `DriverInitResult=ok` signature seen in
`intel27` and `intel28`, and the 640x480 desktop reported for the boot between
them. Those captures therefore describe a boot in which the driver was never
used, not a boot in which it failed.

Two consequences. A capture taken after a lock is worthless if Windows has
booted again in between, because the fallback boot overwrites the trace. And
while the latch is set, no boot can distinguish anything about `Enable`.

## Method note

The guest disk was copied to `Win98HDD.vhd.pre-intel-seg-probe` first,
`SYSTEM.INI` to `C:\WINDOWS\SYSTEM.V9B` and the s3 mini-VDD to
`V9XMINI.S3B` inside the guest. All were restored afterwards and the guest
verified back at its own driver and mini-VDD, `Stage=enable-ok` with the
desktop ready at 1024x768 on boot 597.

`V9XBOOT.INI` was deleted before every test boot. A stale marker read as a live
one is precisely what made the netbook capture ambiguous, and the same mistake
was available here: the guest's previous `Stage` was `enable-ok`, which would
have looked like a clean boot had a test locked before writing anything.
