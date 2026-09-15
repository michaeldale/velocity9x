# The I9XXCODE far call is not what hangs the netbook

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

- The Intel `V9XMINI.VXD` was **not** installed in the guest, which kept the s3
  one. The netbook loads the Intel mini-VDD before the display driver.
- Real Intel hardware, and everything `v9x_display_boot_log` does before the
  `libmain` marker on a machine that has it.
- Anything else among the twenty-five commits since `1f386d0`, of which the
  split is only one.

The netbook's own `Stage=libmain` reading also still rests on an unverified
assumption: that Win9x's `WritePrivateProfileString` rewrites a file when the
value is unchanged. If it skips such a write, `arm_prepare` may have completed
there too and the fault lies later. The `arm-pre` / `arm-in` / `arm-dir` /
`arm-post` markers added in this same change remove that assumption from the
next netbook boot.

## Method note

The guest disk was copied to `Win98HDD.vhd.pre-intel-seg-probe` first, and
`SYSTEM.INI` to `C:\WINDOWS\SYSTEM.V9B` inside the guest. Both were restored
afterwards and the guest verified back at its own driver, desktop ready at
1024x768 on boot 594. The stale `V9XBOOT.INI` was renamed before the first test
boot, because a stale marker read as a live one is precisely what made the
netbook capture ambiguous.
